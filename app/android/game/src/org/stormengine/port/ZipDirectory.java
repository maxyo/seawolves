package org.stormengine.port;

import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.FileChannel;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * The central directory of a single-volume ZIP (APPNOTE 4.3.12-4.3.16, ZIP64 included), read directly from the end
 * of an untrusted file: only the end record and the directory are read, every offset and length is checked against
 * the file and the directory before it is used.
 */
final class ZipDirectory {
    private ZipDirectory() {}

    /** The file is not a single-volume ZIP, or its directory is broken. */
    static class Malformed extends IOException {
        Malformed(String reason) {
            super(reason);
        }
    }

    /** More entries than the caller accepts. */
    static final class TooManyEntries extends Malformed {
        TooManyEntries(long entries) {
            super(entries + " entries");
        }
    }

    /** One entry: its name (UTF-8) and uncompressed size (-1 when a ZIP64 entry does not state it). */
    static final class Entry {
        final String name;
        final long size;

        Entry(String name, long size) {
            this.name = name;
            this.size = size;
        }
    }

    private static final int MAX_DIRECTORY_BYTES = 256 << 20;

    static List<Entry> entries(FileChannel file, long maxEntries) throws IOException {
        long length = file.size();
        int tail = (int) Math.min(length, 22 + 65535);
        ByteBuffer end = read(file, length - tail, tail);
        int at = -1;
        for (int i = tail - 22; i >= 0 && at < 0; i--) if (end.getInt(i) == 0x06054b50) at = i;
        if (at < 0) throw new Malformed("no end of central directory");
        if (end.getShort(at + 4) != 0 || end.getShort(at + 6) != 0)
            throw new Malformed("a multi-volume archive");
        long count = end.getShort(at + 10) & 0xffff,
                size = end.getInt(at + 12) & 0xffffffffL,
                offset = end.getInt(at + 16) & 0xffffffffL;
        if (count == 0xffff || size == 0xffffffffL || offset == 0xffffffffL) {
            if (at < 20 || end.getInt(at - 20) != 0x07064b50)
                throw new Malformed("no ZIP64 end of central directory locator");
            long record = end.getLong(at - 20 + 8);
            if (record < 0 || record > length - 56)
                throw new Malformed("the ZIP64 end record is outside the file");
            ByteBuffer zip64 = read(file, record, 56);
            if (zip64.getInt(0) != 0x06064b50) throw new Malformed("no ZIP64 end record");
            count = zip64.getLong(32);
            size = zip64.getLong(40);
            offset = zip64.getLong(48);
        }
        if (count < 0 || count > maxEntries) throw new TooManyEntries(count);
        if (size < 0 || size > MAX_DIRECTORY_BYTES || offset < 0 || offset > length - size)
            throw new Malformed("the central directory is outside the file");
        ByteBuffer directory = read(file, offset, (int) size);
        List<Entry> entries = new ArrayList<>();
        int pos = 0;
        for (long n = 0; n < count; n++) {
            if (pos > size - 46 || directory.getInt(pos) != 0x02014b50)
                throw new Malformed("a broken central directory entry");
            long bytes = directory.getInt(pos + 24) & 0xffffffffL;
            int name = directory.getShort(pos + 28) & 0xffff,
                    extra = directory.getShort(pos + 30) & 0xffff,
                    comment = directory.getShort(pos + 32) & 0xffff;
            int next = pos + 46 + name + extra + comment;
            if (next > size) throw new Malformed("a broken central directory entry");
            if (bytes == 0xffffffffL) {
                bytes = -1;
                for (int e = pos + 46 + name;
                        e + 4 <= pos + 46 + name + extra;
                        e += 4 + (directory.getShort(e + 2) & 0xffff))
                    if (directory.getShort(e) == 1 && e + 12 <= pos + 46 + name + extra) {
                        bytes = directory.getLong(e + 4);
                        break;
                    }
            }
            byte[] text = new byte[name];
            directory.position(pos + 46);
            directory.get(text);
            entries.add(new Entry(new String(text, StandardCharsets.UTF_8), bytes));
            pos = next;
        }
        return entries;
    }

    private static ByteBuffer read(FileChannel file, long position, int count) throws IOException {
        ByteBuffer buffer = ByteBuffer.allocate(count).order(ByteOrder.LITTLE_ENDIAN);
        while (buffer.hasRemaining())
            if (file.read(buffer, position + buffer.position()) < 0)
                throw new EOFException("ZIP ends early");
        buffer.flip();
        return buffer;
    }
}
