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

}  // namespace
}  // namespace kv
