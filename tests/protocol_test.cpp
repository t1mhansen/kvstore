#include "protocol.h"

#include <gtest/gtest.h>

namespace kv {
namespace {

TEST(ProtocolTest, ParsesGet) {
  const Command command = ParseCommand("GET foo");
  EXPECT_EQ(command.type, CommandType::kGet);
  EXPECT_EQ(command.key, "foo");
}

TEST(ProtocolTest, ParsesDel) {
  const Command command = ParseCommand("DEL foo");
  EXPECT_EQ(command.type, CommandType::kDel);
  EXPECT_EQ(command.key, "foo");
}

TEST(ProtocolTest, ParsesSet) {
  const Command command = ParseCommand("SET foo bar");
  EXPECT_EQ(command.type, CommandType::kSet);
  EXPECT_EQ(command.key, "foo");
  EXPECT_EQ(command.value, "bar");
}

TEST(ProtocolTest, SetValuePreservesInternalSpaces) {
  const Command command = ParseCommand("SET foo bar baz qux");
  EXPECT_EQ(command.type, CommandType::kSet);
  EXPECT_EQ(command.key, "foo");
  EXPECT_EQ(command.value, "bar baz qux");
}

TEST(ProtocolTest, GetWithNoKeyIsInvalid) {
  EXPECT_EQ(ParseCommand("GET").type, CommandType::kInvalid);
}

TEST(ProtocolTest, GetWithTwoTokensIsInvalid) {
  EXPECT_EQ(ParseCommand("GET foo bar").type, CommandType::kInvalid);
}

TEST(ProtocolTest, SetWithNoValueIsInvalid) {
  EXPECT_EQ(ParseCommand("SET foo").type, CommandType::kInvalid);
}

TEST(ProtocolTest, SetWithNothingIsInvalid) {
  EXPECT_EQ(ParseCommand("SET").type, CommandType::kInvalid);
}

TEST(ProtocolTest, EmptyLineIsInvalid) {
  EXPECT_EQ(ParseCommand("").type, CommandType::kInvalid);
}

TEST(ProtocolTest, UnknownVerbIsInvalid) {
  EXPECT_EQ(ParseCommand("FOO bar").type, CommandType::kInvalid);
}

TEST(ProtocolTest, ParsesSetEx) {
  const Command command = ParseCommand("SETEX foo 60 bar");
  EXPECT_EQ(command.type, CommandType::kSetEx);
  EXPECT_EQ(command.key, "foo");
  EXPECT_EQ(command.ttl_seconds, 60u);
  EXPECT_EQ(command.value, "bar");
}

TEST(ProtocolTest, SetExValuePreservesInternalSpaces) {
  const Command command = ParseCommand("SETEX foo 60 bar baz qux");
  EXPECT_EQ(command.value, "bar baz qux");
}

TEST(ProtocolTest, SetExWithNonIntegerTtlIsInvalid) {
  EXPECT_EQ(ParseCommand("SETEX foo soon bar").type, CommandType::kInvalid);
}

TEST(ProtocolTest, SetExWithNegativeTtlIsInvalid) {
  EXPECT_EQ(ParseCommand("SETEX foo -5 bar").type, CommandType::kInvalid);
}

TEST(ProtocolTest, SetExWithNoValueIsInvalid) {
  EXPECT_EQ(ParseCommand("SETEX foo 60").type, CommandType::kInvalid);
}

TEST(ProtocolTest, SetExWithNothingIsInvalid) {
  EXPECT_EQ(ParseCommand("SETEX").type, CommandType::kInvalid);
}

TEST(ProtocolTest, ParsesCompact) {
  EXPECT_EQ(ParseCommand("COMPACT").type, CommandType::kCompact);
}

TEST(ProtocolTest, CompactWithArgumentsIsInvalid) {
  EXPECT_EQ(ParseCommand("COMPACT now").type, CommandType::kInvalid);
}

}  // namespace
}  // namespace kv
