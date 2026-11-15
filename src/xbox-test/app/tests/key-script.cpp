// SPDX-License-Identifier: GPL-3.0-or-later
// cmd.txt keyboard injection parser and timed press/release behavior.
#include "../key_script.h"
#include <cstdio>
using namespace key_script;
#define CHECK(x) do { if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;} } while(0)

int main() {
    Key key{};
    CHECK(parse("13 500",&key)&&key.vk==13&&key.hold_ms==500);
    CHECK(parse("  65 0  ",&key)&&key.vk==65&&key.hold_ms==0);
    CHECK(parse("255 999999",&key)&&key.vk==255&&key.hold_ms==60000);
    CHECK(!parse("0 500",&key));
    CHECK(!parse("256 500",&key));
    CHECK(!parse("A 500",&key));
    CHECK(!parse("13 500 extra",&key));
    CHECK(!parse("13 -1",&key));

    Runner runner;
    CHECK(runner.tick(0)==Edge::none);
    runner.arm({13,100},1000);
    CHECK(runner.tick(1000)==Edge::down);
    CHECK(runner.tick(1099)==Edge::none&&runner.armed&&runner.down);
    CHECK(runner.tick(1100)==Edge::up&&!runner.armed&&!runner.down);
    CHECK(runner.tick(2000)==Edge::none);
    runner.arm({65,500},0);
    CHECK(runner.tick(0)==Edge::down);
    runner.clear();
    CHECK(runner.tick(1000)==Edge::none&&!runner.armed);
    std::puts("key_script: decimal VK parsing, bounded duration and key-down/up timing PASS");
}
