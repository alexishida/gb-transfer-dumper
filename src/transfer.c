/*
 * GB Transfer Dumper - streaming ROM and save transfers.
 */

#include "transfer.h"

#include <libdragon.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app.h"
#include "cart.h"
#include "ui.h"

/* Buffer installed on the streamed file. libtrpak hands over one 32-byte block
 * at a time, so without it the microSD would see writes of that size. */
#define STREAM_BUFFER_SIZE 4096u

/* libtrpak adds this base to the running byte offset of a transfer before
 * passing it to the streaming callbacks; zero makes that argument the file
 * offset itself. */
#define STREAM_BASE ((uintptr_t)0u)

/* Shortest interval between two progress redraws. A redraw costs a full
 * console render, which would otherwise be paid once per 32-byte block. */
#define PROGRESS_INTERVAL_MS 200u

/**
 * @brief The file one transfer streams through, plus its progress state.
 *
 * A single instance is installed as the libtrpak backend's user pointer at
 * startup and re-armed by begin_stream() before each transfer, so the backend
 * never has to be reconfigured — which matters because trpak_configure_io()
 * discards the MBC1M detection that trpak_init() performed.
 */
typedef struct {
    FILE *file;             /**< Stream being written or read. */
    size_t position;        /**< Current offset within that stream. */
    size_t total;           /**< Highest offset reached, for the progress bar. */
    uint32_t checksum;      /**< Running CRC32 of everything stored. */
    int error;              /**< First file error, or ::TRPAK_OK. */
    ui_progress_t progress; /**< Progress screen state. */
    uint64_t last_draw;     /**< timer_ticks() when the screen was last drawn. */
} transfer_stream_t;

static transfer_stream_t stream;
static char stream_buffer[STREAM_BUFFER_SIZE];
static bool backend_installed;

/**
 * @brief Folds one buffer into a running CRC32 (reflected, polynomial EDB88320).
 *
 * @param crc  Running value; start from 0xFFFFFFFF and invert at the end.
 * @param data Bytes to fold in.
 * @param size Number of bytes.
 * @return Updated running value.
 */
static uint32_t update_crc32(uint32_t crc, const uint8_t *data, size_t size)
{
    size_t i;

    for (i = 0u; i < size; i++) {
        int bit;
        crc ^= data[i];
        for (bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

/**
 * @brief Redraws the progress screen, at most once per interval.
 *
 * @param force Draw even when the interval has not elapsed.
 */
static void draw_progress(bool force)
{
    uint64_t now = (uint64_t)timer_ticks();

    if (!force &&
        (now - stream.last_draw) < (uint64_t)TICKS_FROM_MS(PROGRESS_INTERVAL_MS)) {
        return;
    }
    stream.last_draw = now;
    ui_progress_draw(&stream.progress, stream.total);
}

/**
 * @brief Points the stream at an offset, seeking only when it has moved.
 *
 * libtrpak addresses every block by its offset within the transfer rather than
 * by position in a stream, and it repeats an offset when a Transfer Pak reset
 * forces a block to be transferred again. Comparing against the tracked
 * position keeps the common, strictly sequential case free of seeks, which
 * would otherwise flush the buffer above on every block.
 *
 * @param context Stream to position.
 * @param offset  Byte offset requested by libtrpak.
 * @return true when the stream is positioned at @p offset.
 */
static bool seek_stream(transfer_stream_t *context, size_t offset)
{
    if (offset == context->position) {
        return true;
    }
    if (fseek(context->file, (long)offset, SEEK_SET) != 0) {
        debug_log(DEBUG_LEVEL_ERROR, "Seek to offset %lu failed\n",
                  (unsigned long)offset);
        return false;
    }
    context->position = offset;
    return true;
}

/** @brief Default read primitive: libdragon's Joybus accessory read. */
static int transfer_read_block(void *user, int controller, uint16_t address,
                               uint8_t data[TRPAK_TRANSFER_BLOCK_SIZE])
{
    (void)user;
    return joybus_accessory_read(controller, address, data);
}

/** @brief Default write primitive: libdragon's Joybus accessory write. */
static int transfer_write_block(void *user, int controller, uint16_t address,
                                const uint8_t data[TRPAK_TRANSFER_BLOCK_SIZE])
{
    (void)user;
    return joybus_accessory_write(controller, address, data);
}

/** @brief Delay primitive used while libtrpak polls for cartridge readiness. */
static void transfer_delay(void *user, unsigned int milliseconds)
{
    (void)user;
    wait_ms(milliseconds);
}

/**
 * @brief Writes one block libtrpak just read out of the cartridge to the file.
 *
 * Installed as trpak_io::dma_store, which is what lets the bulk helpers stream
 * to the microSD instead of filling a buffer sized for the whole transfer.
 *
 * @param user        The active ::transfer_stream_t.
 * @param source      Block read from the cartridge.
 * @param destination ::STREAM_BASE plus the offset of the block.
 * @param size        Always ::TRPAK_TRANSFER_BLOCK_SIZE.
 * @return `0` on success, `-1` after recording the failure in the stream.
 */
static int transfer_store(void *user, const uint8_t *source,
                          uintptr_t destination, size_t size)
{
    transfer_stream_t *context = (transfer_stream_t *)user;
    size_t offset = (size_t)(destination - STREAM_BASE);

    if (context->file == NULL) {
        context->error = APP_ERR_INVALID_PARAM;
        return -1;
    }

    if (!seek_stream(context, offset)) {
        context->error = APP_ERR_FILE_WRITE;
        return -1;
    }
    if (fwrite(source, 1u, size, context->file) != size) {
        debug_log(DEBUG_LEVEL_ERROR, "Write of %lu bytes at offset %lu failed\n",
                  (unsigned long)size, (unsigned long)offset);
        context->error = APP_ERR_FILE_WRITE;
        return -1;
    }

    context->position = offset + size;
    /* A stored block is never repeated, so the checksum can follow the stream. */
    context->checksum = update_crc32(context->checksum, source, size);
    if (context->position > context->total) {
        context->total = context->position;
    }
    draw_progress(false);
    return 0;
}

/**
 * @brief Reads the block libtrpak is about to write into cartridge RAM.
 *
 * Installed as trpak_io::dma_load. libtrpak repeats an offset when a Transfer
 * Pak reset forces a block to be written again, which seek_stream() turns back
 * into a re-read of the same bytes.
 *
 * @param user        The active ::transfer_stream_t.
 * @param destination Scratch block to fill.
 * @param source      ::STREAM_BASE plus the offset of the block.
 * @param size        Always ::TRPAK_TRANSFER_BLOCK_SIZE.
 * @return `0` on success, `-1` after recording the failure in the stream.
 */
static int transfer_load(void *user, uint8_t *destination, uintptr_t source,
                         size_t size)
{
    transfer_stream_t *context = (transfer_stream_t *)user;
    size_t offset = (size_t)(source - STREAM_BASE);

    if (context->file == NULL) {
        context->error = APP_ERR_INVALID_PARAM;
        return -1;
    }

    if (!seek_stream(context, offset)) {
        context->error = APP_ERR_FILE_READ;
        return -1;
    }
    if (fread(destination, 1u, size, context->file) != size) {
        debug_log(DEBUG_LEVEL_ERROR, "Read of %lu bytes at offset %lu failed\n",
                  (unsigned long)size, (unsigned long)offset);
        context->error = APP_ERR_FILE_READ;
        return -1;
    }

    context->position = offset + size;
    if (context->position > context->total) {
        context->total = context->position;
    }
    draw_progress(false);
    return 0;
}

int transfer_init(void)
{
    const trpak_io io = {
        .read_block = transfer_read_block,
        .write_block = transfer_write_block,
        .delay = transfer_delay,
        .dma_store = transfer_store,
        .dma_load = transfer_load,
        .user = &stream
    };
    int result = trpak_configure_io(&io, JOYPAD_PORT_1, STREAM_BASE);

    backend_installed = result == TRPAK_OK;
    debug_log(backend_installed ? DEBUG_LEVEL_INFO : DEBUG_LEVEL_ERROR,
              "Streaming backend installation: %d\n", result);
    return result;
}

/**
 * @brief Arms the shared stream state and shows the progress screen.
 *
 * @param file      Stream the transfer reads from or writes to.
 * @param operation Headline for the progress screen.
 * @param path      Path shown on that screen.
 * @param expected  Byte count the transfer should move.
 * @retval TRPAK_OK              Ready to transfer.
 * @retval APP_ERR_INVALID_PARAM No file, or the backend was never installed.
 */
static int begin_stream(FILE *file, const char *operation, const char *path,
                        size_t expected)
{
    if (file == NULL) {
        return APP_ERR_INVALID_PARAM;
    }
    if (!backend_installed) {
        /* Reconfiguring here would clear the MBC1M detection made by
         * trpak_init(), so refuse instead: transfer_init() runs at startup. */
        debug_log(DEBUG_LEVEL_ERROR, "Streaming backend is not installed\n");
        return APP_ERR_INVALID_PARAM;
    }

    memset(&stream, 0, sizeof(stream));
    stream.file = file;
    stream.checksum = 0xFFFFFFFFu;
    stream.error = TRPAK_OK;
    /* Batch the 32-byte blocks into microSD-sized accesses. The buffer is
     * static because it has to outlive the fclose() the caller performs. */
    setvbuf(file, stream_buffer, _IOFBF, sizeof(stream_buffer));

    ui_progress_begin(&stream.progress, operation, path, expected);
    stream.last_draw = (uint64_t)timer_ticks();
    return TRPAK_OK;
}

/**
 * @brief Turns a finished transfer into the result the caller reports.
 *
 * A failing streaming callback can only tell libtrpak that the transfer broke,
 * which surfaces as ::TRPAK_ERR_IO; the precise filesystem error is recovered
 * from the stream so the screen names the microSD rather than the accessory.
 *
 * @param result Value returned by the libtrpak bulk helper.
 * @param label  Name for the checksum log line, or NULL when the transfer
 *               stored nothing and therefore has no checksum to report.
 * @return The result to report.
 */
static int finish_stream(int result, const char *label)
{
    if (result == TRPAK_ERR_IO && stream.error != TRPAK_OK) {
        result = stream.error;
    }
    if (result == TRPAK_OK) {
        draw_progress(true);
        if (label != NULL) {
            debug_log(DEBUG_LEVEL_INFO, "%s CRC32: 0x%08X\n", label,
                      (unsigned int)~stream.checksum);
        }
    }
    stream.file = NULL;
    return result;
}

int transfer_dump_rom(FILE *file, const char *path, size_t *bytes_written)
{
    int result;

    *bytes_written = 0u;
    result = begin_stream(file, "Dumping ROM...", path, trcart.romsize);
    if (result != TRPAK_OK) {
        return result;
    }

    result = trpak_read_rom_dma(bytes_written);
    return finish_stream(result, "ROM");
}

int transfer_dump_ram(FILE *file, const char *path, size_t *bytes_written)
{
    int result;

    *bytes_written = 0u;
    if (!cart_has_ram()) {
        debug_log(DEBUG_LEVEL_ERROR, "No RAM present\n");
        return TRPAK_ERR_NO_RAM;
    }

    result = begin_stream(file, "Backing up Save...", path, trcart.ramsize);
    if (result != TRPAK_OK) {
        return result;
    }

    result = trpak_read_save_dma(bytes_written);
    return finish_stream(result, "RAM");
}

int transfer_restore_ram(FILE *file, const char *path, size_t *bytes_read)
{
    int result;

    *bytes_read = 0u;
    if (!cart_has_ram()) {
        debug_log(DEBUG_LEVEL_ERROR, "No RAM present\n");
        return TRPAK_ERR_NO_RAM;
    }

    result = begin_stream(file, "Restoring Save...", path, trcart.ramsize);
    if (result != TRPAK_OK) {
        return result;
    }

    /* Verification is not optional here: it is the only evidence that the
     * cartridge kept what was written to it. */
    result = trpak_write_save_dma(true);
    /* No byte counter is reported by libtrpak on this path, so the stream's
     * own high-water mark stands in for it, including on partial failures. */
    *bytes_read = stream.total;
    if (result == TRPAK_OK) {
        debug_log(DEBUG_LEVEL_INFO, "RAM restore completed: %lu bytes\n",
                  (unsigned long)stream.total);
    }
    return finish_stream(result, NULL);
}
