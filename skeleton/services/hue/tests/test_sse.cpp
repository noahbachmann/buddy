#include <gtest/gtest.h>

#include "sse.hpp"

using buddy::hue::SseParser;
using Events = std::vector<std::string>;

TEST(Sse, Events) {
    SseParser p;
    EXPECT_EQ(p.feed(": hi\n\nid: 1:0\ndata: [1]\n\n"), Events{"[1]"});
    EXPECT_EQ(p.feed("data:[2]\r\n\r\n"), Events{"[2]"});
    EXPECT_EQ(p.feed("data: a\ndata: b\n\n"), Events{"a\nb"});
    EXPECT_EQ(p.feed("event: x\nretry: 5\n\n"), Events{});
}

TEST(Sse, AnyChunking) {
    const std::string stream = "id: 1\ndata: [{\"type\": \"update\"}]\n\nid: 2\ndata: [3]\n\n";
    for (std::size_t size = 1; size <= stream.size(); ++size) {
        SseParser p;
        Events got;
        for (std::size_t i = 0; i < stream.size(); i += size) {
            const auto events = p.feed(std::string_view(stream).substr(i, size));
            got.insert(got.end(), events.begin(), events.end());
        }
        EXPECT_EQ(got, (Events{"[{\"type\": \"update\"}]", "[3]"})) << "chunks of " << size;
    }
}
