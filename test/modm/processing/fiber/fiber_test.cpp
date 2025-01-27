/*
 * Copyright (c) 2020, Erik Henriksson
 * Copyright (c) 2024, Niklas Hauser
 *
 * This file is part of the modm project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
// ----------------------------------------------------------------------------

#include "fiber_test.hpp"
#include "shared.hpp"

#include <modm-test/mock/clock.hpp>

using namespace std::chrono_literals;
using test_clock = modm_test::chrono::milli_clock;

void
FiberTest::setUp()
{
	state = 0;
}

// ================================== FIBER ===================================
static void
f1()
{
	TEST_ASSERT_EQUALS(state++, 0u);
	modm::this_fiber::yield();
	TEST_ASSERT_EQUALS(state++, 1u);
}

void
FiberTest::testOneFiber()
{
	modm::fiber::Task fiber(stack1, f1);
	TEST_ASSERT_DIFFERS(fiber.get_id(), modm::fiber::id(0));
	TEST_ASSERT_TRUE(fiber.joinable());
	modm::fiber::Scheduler::run();
	TEST_ASSERT_FALSE(fiber.joinable());
}

void
FiberTest::testTwoFibers()
{
	modm::fiber::Task fiber1(stack1, []
	{
		TEST_ASSERT_EQUALS(state++, 0u);
		modm::this_fiber::yield(); // goto 1
		TEST_ASSERT_EQUALS(state++, 2u);
		modm::this_fiber::yield();
		TEST_ASSERT_EQUALS(state++, 3u);
		modm::this_fiber::yield();
		TEST_ASSERT_EQUALS(state++, 4u);
		modm::this_fiber::yield();
		TEST_ASSERT_EQUALS(state++, 5u);
	});
	modm::fiber::Task fiber2(stack2, [&]
	{
		TEST_ASSERT_EQUALS(state++, 1u);
		TEST_ASSERT_TRUE(fiber1.joinable());
		TEST_ASSERT_FALSE(fiber2.joinable());
		fiber1.join(); // goto 2
		TEST_ASSERT_EQUALS(state++, 6u);
		fiber2.join(); // should not hang
		TEST_ASSERT_EQUALS(state++, 7u);
	});
	modm::fiber::Scheduler::run();
}

static __attribute__((noinline)) void
subroutine()
{
	TEST_ASSERT_EQUALS(state++, 2u);
	modm::this_fiber::yield(); // goto 3
	TEST_ASSERT_EQUALS(state++, 4u);
}

void
FiberTest::testYieldFromSubroutine()
{
	modm::fiber::Task fiber1(stack1, []
	{
		TEST_ASSERT_EQUALS(state++, 0u);
		modm::this_fiber::yield(); // goto 1
		TEST_ASSERT_EQUALS(state++, 3u);
	});
	modm::fiber::Task fiber2(stack2, [&]
	{
		TEST_ASSERT_EQUALS(state++, 1u);
		TEST_ASSERT_TRUE(fiber1.joinable());
		subroutine();
		TEST_ASSERT_FALSE(fiber1.joinable());
		TEST_ASSERT_EQUALS(state++, 5u);
	});
	modm::fiber::Scheduler::run();
}


void
FiberTest::testPollFor()
{
	test_clock::setTime(1251);
	TEST_ASSERT_TRUE(modm::this_fiber::poll_for(20ms, []{ return true; }));
	// timeout is tested in the SleepFor() test
}


void
FiberTest::testPollUntil()
{
	test_clock::setTime(451250);
	TEST_ASSERT_TRUE(modm::this_fiber::poll_until(modm::Clock::now() + 20ms, []{ return true; }));
	TEST_ASSERT_TRUE(modm::this_fiber::poll_until(modm::Clock::now() - 20ms, []{ return true; }));
	// timeout is tested in the SleepUntil() tests
}


static void
f4()
{
	TEST_ASSERT_EQUALS(state++, 0u);
	modm::this_fiber::sleep_for(50ms); // goto 1
	TEST_ASSERT_EQUALS(state++, 5u);
}

static void
f5()
{
	TEST_ASSERT_EQUALS(state++, 1u);
	test_clock::increment(10);
	TEST_ASSERT_EQUALS(state++, 2u);
	modm::this_fiber::yield();

	test_clock::increment(20);
	TEST_ASSERT_EQUALS(state++, 3u);
	modm::this_fiber::yield();

	test_clock::increment(30);
	TEST_ASSERT_EQUALS(state++, 4u);
	modm::this_fiber::yield(); // goto 5

	TEST_ASSERT_EQUALS(state++, 6u);
}

static void
runSleepFor(uint32_t startTime)
{
	test_clock::setTime(startTime);
	modm::fiber::Task fiber1(stack1, f4), fiber2(stack2, f5);
	modm::fiber::Scheduler::run();
}


void
FiberTest::testSleepFor()
{
	runSleepFor(16203);
	state = 0;
	runSleepFor(0xffff'ffff - 30);
}

static void
f6()
{
	TEST_ASSERT_EQUALS(state++, 0u); // goto 1
	modm::this_fiber::sleep_until(modm::Clock::now() + 50ms);
	TEST_ASSERT_EQUALS(state++, 5u);
	modm::this_fiber::yield(); // goto 6
	modm::this_fiber::sleep_until(modm::Clock::now() - 50ms);
	TEST_ASSERT_EQUALS(state++, 7u);
}

static void
runSleepUntil(uint32_t startTime)
{
	test_clock::setTime(startTime);
	modm::fiber::Task fiber1(stack1, f6), fiber2(stack2, f5);
	modm::fiber::Scheduler::run();
}

void
FiberTest::testSleepUntil()
{
	runSleepUntil(1502);
	state = 0;
	runSleepUntil(0xffff'ffff - 30);
}

static void
f7(modm::fiber::stop_token stoken)
{
	TEST_ASSERT_EQUALS(state++, 0u);
	TEST_ASSERT_TRUE(stoken.stop_possible());
	TEST_ASSERT_FALSE(stoken.stop_requested());
	while(not stoken.stop_requested())
	{
		modm::this_fiber::yield(); // goto 1
		state++; // 2, 4
	}
	TEST_ASSERT_TRUE(stoken.stop_requested());
	TEST_ASSERT_EQUALS(state++, 5u);
}

void
FiberTest::testStopToken()
{
	modm::fiber::Task fiber1(stack1, f7);
	modm::fiber::Task fiber2(stack2, [&](modm::fiber::stop_token stoken)
	{
		TEST_ASSERT_EQUALS(state++, 1u);
		TEST_ASSERT_TRUE(stoken.stop_possible());
		TEST_ASSERT_FALSE(stoken.stop_requested());
		modm::this_fiber::yield(); // goto 2
		TEST_ASSERT_EQUALS(state++, 3u);
		fiber1.request_stop();
		modm::this_fiber::yield(); // goto 4
		TEST_ASSERT_EQUALS(state++, 6u);
	});
	modm::fiber::Scheduler::run();
}
