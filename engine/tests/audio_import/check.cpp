// audio_import on inputs made here (Vorbis packets from libvorbis, FSB5 images, banks, string tables, WAV files),
// decoded with stb_vorbis. "[corpus]": every bank and WAV of the installed game in $STORM_GAME_DIR (not run without
// one): each bank sample remuxes and decodes to its FSB5 length, channels and rate, each string table reads, one WAV
// of every format encodes and decodes.
#include "audio_import/audio_import.hpp"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <set>
#include <string>
#include <tuple>

#include <catch2/catch.hpp>

using namespace storm::audio_import;

namespace
{
void put16(Bytes &out, uint32_t v)
{
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}
void put24(Bytes &out, uint32_t v)
{
    put16(out, v & 0xffff);
    out.push_back(static_cast<uint8_t>(v >> 16));
}
void put32(Bytes &out, uint32_t v)
{
    put16(out, v & 0xffff);
    put16(out, v >> 16);
}
void put64(Bytes &out, uint64_t v)
{
    put32(out, static_cast<uint32_t>(v));
    put32(out, static_cast<uint32_t>(v >> 32));
}
void putText(Bytes &out, const std::string &text)
{
    out.insert(out.end(), text.begin(), text.end());
}
Bytes chunk(const std::string &id, const Bytes &body)
{
    Bytes out;
    putText(out, id);
    put32(out, static_cast<uint32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
    if (body.size() & 1)
        out.push_back(0);
    return out;
}
Bytes list(const std::string &type, const std::vector<Bytes> &children)
{
    Bytes body;
    putText(body, type);
    for (const auto &child : children)
        body.insert(body.end(), child.begin(), child.end());
    return chunk("LIST", body);
}

std::vector<float> tone(uint32_t frames, uint32_t channels, uint32_t rate)
{
    std::vector<float> pcm(size_t{frames} * channels);
    for (uint32_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < channels; ++c)
            pcm[size_t{i} * channels + c] = 0.5f * std::sin(2.0 * std::numbers::pi * (440.0 + 220.0 * c) * i / rate);
    return pcm;
}

// --- libvorbis reference encode: header packets + raw audio packets ------------

struct Encoded
{
    Bytes identification, setup;
    std::vector<Bytes> audio;
    std::vector<int64_t> granules; // libvorbis' own position after each audio packet
    uint32_t frames;
};

Encoded encodeRaw(const std::vector<float> &pcm, uint32_t channels, uint32_t rate, float quality)
{
    Encoded out;
    out.frames = static_cast<uint32_t>(pcm.size() / channels);
    vorbis_info info;
    vorbis_info_init(&info);
    if (vorbis_encode_init_vbr(&info, channels, rate, quality) != 0)
        throw std::runtime_error("encoder init");
    vorbis_dsp_state dsp;
    vorbis_block block;
    vorbis_comment comment;
    vorbis_comment_init(&comment);
    vorbis_analysis_init(&dsp, &info);
    vorbis_block_init(&dsp, &block);
    ogg_packet id, cm, setup;
    vorbis_analysis_headerout(&dsp, &comment, &id, &cm, &setup);
    out.identification.assign(id.packet, id.packet + id.bytes);
    out.setup.assign(setup.packet, setup.packet + setup.bytes);
    float **buffer = vorbis_analysis_buffer(&dsp, static_cast<int>(out.frames));
    for (uint32_t i = 0; i < out.frames; ++i)
        for (uint32_t c = 0; c < channels; ++c)
            buffer[c][i] = pcm[size_t{i} * channels + c];
    vorbis_analysis_wrote(&dsp, static_cast<int>(out.frames));
    vorbis_analysis_wrote(&dsp, 0);
    while (vorbis_analysis_blockout(&dsp, &block) == 1)
    {
        vorbis_analysis(&block, nullptr);
        vorbis_bitrate_addblock(&block);
        ogg_packet packet;
        while (vorbis_bitrate_flushpacket(&dsp, &packet) == 1)
        {
            out.audio.emplace_back(packet.packet, packet.packet + packet.bytes);
            out.granules.push_back(packet.granulepos);
        }
    }
    vorbis_block_clear(&block);
    vorbis_dsp_clear(&dsp);
    vorbis_comment_clear(&comment);
    vorbis_info_clear(&info);
    return out;
}

// --- FSB5 / bank builders --------------------------------------------------------

struct SampleSpec
{
    std::string name;
    uint32_t channels, rate;
    Encoded encoded;
    std::optional<std::pair<uint32_t, uint32_t>> loop;
    uint32_t crcOverride = 0;
};

uint32_t frequencyCode(uint32_t rate)
{
    return rate == 48000 ? 9 : rate == 44100 ? 8 : 0; // 0: frequency chunk
}

Bytes buildFsb5(const std::vector<SampleSpec> &samples)
{
    Bytes headers, names, data;
    std::vector<uint32_t> nameOffsets;
    Bytes nameStrings;
    for (const auto &s : samples)
    {
        while (data.size() % 16)
            data.push_back(0);
        const uint32_t offset = static_cast<uint32_t>(data.size());
        for (const auto &packet : s.encoded.audio)
        {
            put16(data, static_cast<uint32_t>(packet.size()));
            data.insert(data.end(), packet.begin(), packet.end());
        }
        put16(data, 0);
        Bytes vorbisData;
        put32(vorbisData, s.crcOverride ? s.crcOverride : crc32(s.encoded.setup));
        put32(vorbisData, 0); // empty seek table header (ignored by the importer)
        std::vector<std::pair<uint32_t, Bytes>> typed;
        if (s.channels > 2)
            typed.emplace_back(1, Bytes{static_cast<uint8_t>(s.channels)});
        if (!frequencyCode(s.rate))
        {
            Bytes f;
            put32(f, s.rate);
            typed.emplace_back(2, f);
        }
        if (s.loop)
        {
            Bytes l;
            put32(l, s.loop->first);
            put32(l, s.loop->second);
            typed.emplace_back(3, l);
        }
        typed.emplace_back(13, Bytes{1, 2, 3, 4}); // an unrelated chunk type the importer must skip
        typed.emplace_back(11, vorbisData);
        uint64_t raw = 1; // more chunks follow
        raw |= uint64_t{frequencyCode(s.rate)} << 1;
        raw |= uint64_t{s.channels == 2 ? 1u : 0u} << 5;
        raw |= uint64_t{offset / 16} << 6;
        raw |= uint64_t{s.encoded.frames} << 34;
        put64(headers, raw);
        for (size_t i = 0; i < typed.size(); ++i)
        {
            const bool more = i + 1 < typed.size();
            put32(headers,
                  (more ? 1u : 0u) | static_cast<uint32_t>(typed[i].second.size()) << 1 | typed[i].first << 25);
            headers.insert(headers.end(), typed[i].second.begin(), typed[i].second.end());
        }
        nameOffsets.push_back(static_cast<uint32_t>(nameStrings.size()));
        putText(nameStrings, s.name);
        nameStrings.push_back(0);
    }
    for (uint32_t o : nameOffsets)
        put32(names, static_cast<uint32_t>(o + 4 * samples.size()));
    names.insert(names.end(), nameStrings.begin(), nameStrings.end());
    while (names.size() % 16)
        names.push_back(0);
    Bytes fsb;
    putText(fsb, "FSB5");
    put32(fsb, 1);
    put32(fsb, static_cast<uint32_t>(samples.size()));
    put32(fsb, static_cast<uint32_t>(headers.size()));
    put32(fsb, static_cast<uint32_t>(names.size()));
    put32(fsb, static_cast<uint32_t>(data.size()));
    put32(fsb, 15);
    fsb.resize(fsb.size() + 32, 0);
    fsb.insert(fsb.end(), headers.begin(), headers.end());
    fsb.insert(fsb.end(), names.begin(), names.end());
    fsb.insert(fsb.end(), data.begin(), data.end());
    return fsb;
}

// RIFF "FEV " bank: FMT, LIST PROJ {BNKI, SNDH or STDT}, then the SND chunk.
Bytes buildBank(const Bytes &fsb, const Bytes *stringTable)
{
    Bytes fmt(8, 0);
    Bytes bnki(32, 0x11);
    const size_t pad = 20;
    auto assemble = [&](uint32_t fsbOffset) {
        Bytes sndh;
        if (!fsb.empty())
        {
            put16(sndh, 1 << 1 | 1);
            put16(sndh, 8);
            put32(sndh, fsbOffset);
            put32(sndh, static_cast<uint32_t>(fsb.size()));
        }
        std::vector<Bytes> proj{chunk("BNKI", bnki), chunk("SNDH", sndh)};
        if (stringTable)
            proj.push_back(chunk("STDT", *stringTable));
        Bytes body;
        putText(body, "FEV ");
        for (const Bytes &part : {chunk("FMT ", fmt), list("PROJ", proj)})
            body.insert(body.end(), part.begin(), part.end());
        if (!fsb.empty())
        {
            Bytes snd(pad, 0);
            snd.insert(snd.end(), fsb.begin(), fsb.end());
            const Bytes sndChunk = chunk("SND ", snd);
            body.insert(body.end(), sndChunk.begin(), sndChunk.end());
        }
        Bytes riff;
        putText(riff, "RIFF");
        put32(riff, static_cast<uint32_t>(body.size()));
        riff.insert(riff.end(), body.begin(), body.end());
        return riff;
    };
    Bytes probe = assemble(0);
    // The FSB5 image sits at the end of the file, after the SND padding.
    const uint32_t offset = static_cast<uint32_t>(probe.size() - fsb.size() - (fsb.size() & 1));
    return assemble(offset);
}

// --- Ogg inspection ---------------------------------------------------------------

struct OggPackets
{
    std::vector<Bytes> packets;
    std::vector<int64_t> granules;
};

OggPackets readOgg(const Bytes &ogg)
{
    OggPackets out;
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    char *buffer = ogg_sync_buffer(&sync, static_cast<long>(ogg.size()));
    std::memcpy(buffer, ogg.data(), ogg.size());
    ogg_sync_wrote(&sync, static_cast<long>(ogg.size()));
    ogg_stream_state stream;
    bool started = false;
    ogg_page page;
    while (ogg_sync_pageout(&sync, &page) == 1)
    {
        if (!started)
        {
            ogg_stream_init(&stream, ogg_page_serialno(&page));
            started = true;
        }
        ogg_stream_pagein(&stream, &page);
        ogg_packet packet;
        while (ogg_stream_packetout(&stream, &packet) == 1)
        {
            out.packets.emplace_back(packet.packet, packet.packet + packet.bytes);
            out.granules.push_back(packet.granulepos);
        }
    }
    if (started)
        ogg_stream_clear(&stream);
    ogg_sync_clear(&sync);
    return out;
}

struct Decoded
{
    int channels = 0, rate = 0, frames = -1;
    std::vector<short> pcm;
};

Decoded decode(const Bytes &ogg)
{
    Decoded out;
    short *pcm = nullptr;
    out.frames = stb_vorbis_decode_memory(ogg.data(), static_cast<int>(ogg.size()), &out.channels, &out.rate, &pcm);
    if (out.frames > 0)
        out.pcm.assign(pcm, pcm + size_t(out.frames) * out.channels);
    std::free(pcm);
    return out;
}

double snrDb(const std::vector<float> &reference, const std::vector<short> &decoded)
{
    double signal = 0, noise = 0;
    for (size_t i = 0; i < reference.size() && i < decoded.size(); ++i)
    {
        const double d = decoded[i] / 32768.0 - reference[i];
        signal += double(reference[i]) * reference[i];
        noise += d * d;
    }
    return 10 * std::log10(signal / std::max(noise, 1e-20));
}

// --- checks -------------------------------------------------------------------------

TEST_CASE("FSB5 Vorbis remux")
{
    const std::string check = "123456789";
    CHECK(crc32(ByteView(reinterpret_cast<const uint8_t *>(check.data()), check.size())) ==
          0xcbf43926u);                          // CRC-32/ISO-HDLC check value
    const auto monoPcm = tone(240000, 1, 48000); // long enough for several Ogg pages
    const auto stereoPcm = tone(30001, 2, 44100);
    const auto surroundPcm = tone(9000, 6, 32000);
    std::vector<SampleSpec> specs{
        {"mono_48k", 1, 48000, encodeRaw(monoPcm, 1, 48000, 0.7f), std::nullopt},
        {"stereo/loop 44k", 2, 44100, encodeRaw(stereoPcm, 2, 44100, 0.4f), std::make_pair(100u, 30000u)},
        {"surround_32k", 6, 32000, encodeRaw(surroundPcm, 6, 32000, 0.1f), std::nullopt},
    };
    const Bytes fsb = buildFsb5(specs);
    const Bytes bank = buildBank(fsb, nullptr);

    const auto images = findFsb5(bank);
    CHECK((images.size() == 1 && images[0].size() == fsb.size() &&
           std::equal(images[0].begin(), images[0].end(), fsb.begin())));
    const Fsb5 parsed = parseFsb5(images.at(0));
    CHECK((parsed.codec == static_cast<uint32_t>(Fsb5Codec::Vorbis) && parsed.samples.size() == specs.size()));
    VorbisHeaderCache cache;
    const std::vector<const std::vector<float> *> sources{&monoPcm, &stereoPcm, &surroundPcm};
    for (size_t i = 0; i < specs.size(); ++i)
    {
        const auto &spec = specs[i];
        const auto &sample = parsed.samples.at(i);
        INFO("sample " << spec.name);
        CHECK((sample.name == spec.name && sample.channels == spec.channels && sample.frequency == spec.rate &&
               sample.sampleCount == spec.encoded.frames && sample.loop == spec.loop &&
               sample.vorbisSetupCrc == crc32(spec.encoded.setup)));
        CHECK(std::find(sample.chunkTypes.begin(), sample.chunkTypes.end(), 13u) != sample.chunkTypes.end());

        const Bytes ogg = remuxFsb5Vorbis(sample, cache);
        const OggPackets packets = readOgg(ogg);
        CHECK(packets.packets.size() == spec.encoded.audio.size() + 3);
        CHECK((packets.packets.size() > 3 && packets.packets[0] == spec.encoded.identification &&
               packets.packets[2] == spec.encoded.setup));
        bool same = packets.packets.size() == spec.encoded.audio.size() + 3;
        for (size_t p = 0; same && p < spec.encoded.audio.size(); ++p)
            same = packets.packets[p + 3] == spec.encoded.audio[p];
        CHECK(same);
        CHECK((!packets.granules.empty() && packets.granules.back() == spec.encoded.frames));
        // A page's position is the encoder's own for the packet that ends it.
        for (size_t p = 3; p < packets.granules.size() && p - 3 < spec.encoded.granules.size(); ++p)
            if (packets.granules[p] != -1)
                CHECK(packets.granules[p] == spec.encoded.granules[p - 3]);

        const Decoded decoded = decode(ogg);
        CHECK((decoded.frames == int(spec.encoded.frames) && decoded.channels == int(spec.channels) &&
               decoded.rate == int(spec.rate)));
        const double snr = snrDb(*sources[i], decoded.pcm);
        CHECK(snr > 12);
    }

    auto unknown = specs;
    unknown.resize(1);
    unknown[0].crcOverride = 0x12345678;
    const Fsb5 bad = parseFsb5(buildFsb5(unknown));
    CHECK_THROWS_WITH(remuxFsb5Vorbis(bad.samples.at(0), cache), Catch::Contains("12345678"));

    CHECK_THROWS_WITH(parseFsb5(ByteView(fsb).first(fsb.size() - 40)), Catch::Contains("FSB5"));
    Bytes wrongSndh = bank;
    const std::string needle = "SNDH";
    const size_t sndh =
        std::search(wrongSndh.begin(), wrongSndh.end(), needle.begin(), needle.end()) - wrongSndh.begin();
    wrongSndh.at(sndh + 12) ^= 0x01; // offset no longer points at the FSB5 magic
    CHECK_THROWS_WITH(findFsb5(wrongSndh), Catch::Contains("SNDH"));
    CHECK(findFsb5(buildBank({}, nullptr)).empty());
}

Bytes wav(uint16_t tag, uint32_t channels, uint32_t rate, uint32_t bits, const std::vector<float> &pcm, bool extensible)
{
    Bytes fmt;
    put16(fmt, extensible ? 0xfffe : tag);
    put16(fmt, channels);
    put32(fmt, rate);
    put32(fmt, rate * channels * bits / 8);
    put16(fmt, channels * bits / 8);
    put16(fmt, bits);
    if (extensible)
    {
        put16(fmt, 22);
        put16(fmt, bits);
        put32(fmt, 0);
        put16(fmt, tag);
        const uint8_t tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
        fmt.insert(fmt.end(), tail, tail + 14);
    }
    Bytes data;
    for (float v : pcm)
    {
        if (tag == 3)
        {
            uint32_t b;
            std::memcpy(&b, &v, 4);
            put32(data, b);
        }
        else if (bits == 8)
            data.push_back(static_cast<uint8_t>(std::lround(v * 127) + 128));
        else if (bits == 16)
            put16(data, static_cast<uint16_t>(std::lround(v * 32767)));
        else if (bits == 24)
            put24(data, static_cast<uint32_t>(std::lround(v * 8388607)) & 0xffffff);
        else
            put32(data, static_cast<uint32_t>(std::lround(double(v) * 2147483647.0)));
    }
    Bytes body;
    putText(body, "WAVE");
    for (const Bytes &part : {chunk("fmt ", fmt), chunk("LIST", Bytes{'I', 'N', 'F', 'O', 0}), chunk("data", data)})
        body.insert(body.end(), part.begin(), part.end());
    Bytes riff;
    putText(riff, "RIFF");
    put32(riff, static_cast<uint32_t>(body.size()));
    riff.insert(riff.end(), body.begin(), body.end());
    return riff;
}

TEST_CASE("WAV encode")
{
    struct Case
    {
        const char *name;
        uint16_t tag;
        uint32_t channels, rate, bits;
        bool extensible;
    };
    const Case cases[] = {
        {"pcm8 mono 22050", 1, 1, 22050, 8, false},     {"pcm16 stereo 44100", 1, 2, 44100, 16, false},
        {"pcm24 stereo 48000", 1, 2, 48000, 24, false}, {"pcm32 mono 48000", 1, 1, 48000, 32, false},
        {"float mono 48000", 3, 1, 48000, 32, false},   {"extensible pcm16 stereo", 1, 2, 44100, 16, true},
    };
    for (const auto &c : cases)
    {
        INFO(c.name);
        const uint32_t frames = c.rate / 3 + 7;
        const auto pcm = tone(frames, c.channels, c.rate);
        const Bytes input = wav(c.tag, c.channels, c.rate, c.bits, pcm, c.extensible);
        const WavInfo info = readWavInfo(input);
        CHECK((info.channels == c.channels && info.rate == c.rate && info.frames == frames &&
               info.isFloat == (c.tag == 3)));
        const Decoded decoded = decode(encodeWavToVorbis(input, 0.4f));
        CHECK((decoded.frames == int(frames) && decoded.channels == int(c.channels) && decoded.rate == int(c.rate)));
        const double snr = snrDb(pcm, decoded.pcm);
        CHECK(snr > 12);
    }
    const auto pcm = tone(1000, 1, 22050);
    CHECK_THROWS_WITH(encodeWavToVorbis(wav(2, 1, 22050, 16, pcm, false), 0.4f), Catch::Contains("format tag 2"));
    Bytes truncated = wav(1, 1, 22050, 16, pcm, false);
    truncated.resize(truncated.size() - 100);
    CHECK_THROWS_WITH(encodeWavToVorbis(truncated, 0.4f), Catch::Contains("exceeds"));
}

Bytes buildStringTable(bool breakParent)
{
    // Paths: 0 "bank:/Master", 1 "event:/A", 2 "event:/AB", 3 "event:/C"
    struct R
    {
        uint32_t label;
        char first;
        uint32_t child;
        uint8_t count;
    };
    const std::string pool = std::string("bank:/Master\0event:/\0A\0C\0B\0", 27);
    const R records[] = {
        {0xffffff, 0, 1, 2}, // root
        {0, 'b', 0, 0},      // "bank:/Master"            -> string 0
        {13, 'e', 3, 2},     // "event:/"
        {21, 'a', 5, 2},     // "A"
        {23, 'c', 3, 0},     // "C"                       -> string 3
        {0xffffff, 0, 1, 0}, // end of "event:/A"         -> string 1
        {25, 'b', 2, 0},     // "B"                       -> string 2
    };
    const uint32_t stringRecords[] = {1, 5, 6, 4};
    const uint32_t parents[] = {0xffffff, 0, 0, 2, 2, 3, breakParent ? 2u : 3u};
    Bytes out;
    put32(out, 1);
    put16(out, 7 << 1 | 1);
    put16(out, 8);
    for (const auto &r : records)
    {
        put24(out, r.label);
        out.push_back(static_cast<uint8_t>(r.first));
        put24(out, r.child);
        out.push_back(r.count);
    }
    put16(out, 4 << 1 | 1);
    put16(out, 16);
    for (uint8_t g = 0; g < 4; ++g)
        for (uint8_t b = 0; b < 16; ++b)
            out.push_back(static_cast<uint8_t>(g * 16 + b));
    put16(out, static_cast<uint32_t>(pool.size()));
    putText(out, pool);
    put16(out, 4);
    for (uint32_t r : stringRecords)
        put24(out, r);
    put16(out, 7);
    for (uint32_t p : parents)
        put24(out, p);
    return out;
}

TEST_CASE("bank string table")
{
    const Bytes table = buildStringTable(false);
    const auto entries = readStringTable(buildBank({}, &table));
    const char *paths[] = {"bank:/Master", "event:/A", "event:/AB", "event:/C"};
    CHECK(entries.size() == 4);
    for (size_t i = 0; i < entries.size() && i < 4; ++i)
        CHECK(entries[i].path == paths[i]);
    CHECK((entries.size() == 4 && entries[1].guid.toString() == "{13121110-1514-1716-1819-1a1b1c1d1e1f}"));
    const Bytes broken = buildStringTable(true);
    CHECK_THROWS_WITH(readStringTable(buildBank({}, &broken)), Catch::Contains("parent"));
    CHECK_THROWS_WITH(readStringTable(buildBank({}, nullptr)), Catch::Contains("STDT"));
}

Bytes readFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
} // namespace

TEST_CASE("audio of an installed game", "[.corpus]")
{
    const char *root = std::getenv("STORM_GAME_DIR");
    if (!root || !*root)
    {
        std::puts("NOT_RUN: set STORM_GAME_DIR to an installed game");
        std::exit(77);
    }
    VorbisHeaderCache cache;
    size_t banks = 0, samples = 0, remuxed = 0, strings = 0, wavs = 0, encoded = 0;
    std::set<std::tuple<uint32_t, uint32_t, uint32_t, bool>> formats;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    {
        const std::string name = lower(entry.path().filename().string());
        if (!entry.is_regular_file())
            continue;
        INFO(entry.path().string());
        if (name.ends_with(".strings.bank"))
        {
            strings += readStringTable(readFile(entry.path())).size();
            continue;
        }
        if (name.ends_with(".bank"))
        {
            ++banks;
            const Bytes bank = readFile(entry.path());
            for (const ByteView image : findFsb5(bank))
            {
                const Fsb5 fsb = parseFsb5(image);
                REQUIRE(fsb.codec == static_cast<uint32_t>(Fsb5Codec::Vorbis));
                for (const Fsb5Sample &sample : fsb.samples)
                {
                    ++samples;
                    INFO(sample.name);
                    const Decoded decoded = decode(remuxFsb5Vorbis(sample, cache));
                    CHECK(decoded.frames == int(sample.sampleCount));
                    CHECK((decoded.channels == int(sample.channels) && decoded.rate == int(sample.frequency)));
                    remuxed += decoded.frames == int(sample.sampleCount);
                }
            }
        }
        else if (name.ends_with(".wav"))
        {
            ++wavs;
            const Bytes wav = readFile(entry.path());
            const WavInfo info = readWavInfo(wav);
            if (!formats.emplace(info.channels, info.rate, info.bitsPerSample, info.isFloat).second)
                continue;
            const Decoded decoded = decode(encodeWavToVorbis(wav, 0.5f));
            CHECK((decoded.frames == int(info.frames) && decoded.channels == int(info.channels) &&
                   decoded.rate == int(info.rate)));
            ++encoded;
        }
    }
    std::printf(
        "%s: %zu banks, %zu of %zu samples remuxed and decoded to their length, %zu strings, %zu WAV files read, "
        "%zu formats encoded\n",
        root, banks, remuxed, samples, strings, wavs, encoded);
}
