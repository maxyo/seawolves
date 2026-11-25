#include "message.h"
#include <iostream>
#include <string>
#include <stdexcept>

#include <catch2/catch.hpp>

static void resetVa(MESSAGE &message, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    message.ResetVA(format, args);
    va_end(args);
}

static MessageFormatError mismatch(MESSAGE &message, char requested)
{
    try
    {
        message.ValidateFormat(requested);
    }
    catch (const MessageFormatError &error)
    {
        return error;
    }
    throw std::runtime_error("Mismatch did not throw");
}
TEST_CASE("message_diagnostics")
{
    MESSAGE message;
    message.Reset("ls", int32_t{1001}, std::string("payload"));
    REQUIRE(message.Long() == 1001);
    auto error = mismatch(message, 'f');
    std::string text = error.what();
    for (const auto *piece :
         {"Incorrect message data", "expected='f'", "actual='s'", "parameter=1", "format='ls'", "messageCode=1001"})
        REQUIRE(text.find(piece) != std::string::npos);
    REQUIRE(message.String() == "payload"); // Failed validation did not consume the field.
    auto end = mismatch(message, 'l');
    text = end.what();
    REQUIRE((text.find("actual=<end>") != std::string::npos && text.find("parameter=2") != std::string::npos));
    for (int i = 0; i < 5; ++i)
        REQUIRE(std::string(mismatch(message, 's').what()).find("actual=<end>") != std::string::npos);
    MESSAGE empty;
    auto missing = mismatch(empty, 'l');
    REQUIRE(std::string(missing.what()).find("Read from empty message") != std::string::npos);
    // Delivery boundaries enrich the same exception; the caller still receives the failure.
    bool propagated = false;
    try
    {
        try
        {
            throw error;
        }
        catch (MessageFormatError &inner)
        {
            inner.AppendContext("script SendMessage destination=42 receiver=worldmap source=worldmap.c:155");
            throw;
        }
    }
    catch (const std::runtime_error &outer)
    {
        propagated = true;
        text = outer.what();
        REQUIRE((text.find("expected='f'") != std::string::npos &&
                 text.find("receiver=worldmap") != std::string::npos &&
                 text.find("worldmap.c:155") != std::string::npos));
    }
    REQUIRE(propagated);
    message.Move2Start();
    REQUIRE((message.Long() == 1001 && message.String() == "payload"));
    message.Reset("lf", int32_t{7}, 1.5f);
    REQUIRE((message.Long() == 7 && message.Float() == 1.5f));
    char source[] = "owned";
    resetVa(message, "ls", int32_t{9}, source);
    source[0] = 'X';
    REQUIRE((message.Long() == 9 && message.String() == "owned")); // Delivery no longer needs a live va_list.
    std::cout << "PASS real MESSAGE expected/actual/format/index/code diagnostics, exhausted/empty bounds, unchanged "
                 "cursor, enriched rethrow and valid reads\n";
}
