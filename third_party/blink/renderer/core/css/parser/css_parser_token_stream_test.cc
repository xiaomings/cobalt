// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/css/parser/css_parser_token_stream.h"

#include "testing/gtest/include/gtest/gtest.h"

#include "third_party/blink/renderer/platform/wtf/text/string_builder.h"

namespace blink {

namespace {

TEST(CSSParserTokenStreamTest, EmptyStream) {
  CSSTokenizer tokenizer(String(""));
  CSSParserTokenStream stream(tokenizer);
  EXPECT_TRUE(stream.Consume().IsEOF());
  EXPECT_TRUE(stream.Peek().IsEOF());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, PeekThenConsume) {
  CSSTokenizer tokenizer(String("A"));  // kIdent
  CSSParserTokenStream stream(tokenizer);
  EXPECT_EQ(kIdentToken, stream.Peek().GetType());
  EXPECT_EQ(kIdentToken, stream.Consume().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, ConsumeThenPeek) {
  CSSTokenizer tokenizer(String("A"));  // kIdent
  CSSParserTokenStream stream(tokenizer);
  EXPECT_EQ(kIdentToken, stream.Consume().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, ConsumeMultipleTokens) {
  CSSTokenizer tokenizer(String("A 1"));  // kIdent kWhitespace kNumber
  CSSParserTokenStream stream(tokenizer);
  EXPECT_EQ(kIdentToken, stream.Consume().GetType());
  EXPECT_EQ(kWhitespaceToken, stream.Consume().GetType());
  EXPECT_EQ(kNumberToken, stream.Consume().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, UncheckedPeekAndConsumeAfterPeek) {
  CSSTokenizer tokenizer(String("A"));  // kIdent
  CSSParserTokenStream stream(tokenizer);
  EXPECT_EQ(kIdentToken, stream.Peek().GetType());
  EXPECT_EQ(kIdentToken, stream.UncheckedPeek().GetType());
  EXPECT_EQ(kIdentToken, stream.UncheckedConsume().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, UncheckedPeekAndConsumeAfterAtEnd) {
  CSSTokenizer tokenizer(String("A"));  // kIdent
  CSSParserTokenStream stream(tokenizer);
  EXPECT_FALSE(stream.AtEnd());
  EXPECT_EQ(kIdentToken, stream.UncheckedPeek().GetType());
  EXPECT_EQ(kIdentToken, stream.UncheckedConsume().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, UncheckedConsumeComponentValue) {
  CSSTokenizer tokenizer(String("A{1}{2{3}}B"));
  CSSParserTokenStream stream(tokenizer);

  EXPECT_EQ(kIdentToken, stream.Peek().GetType());
  stream.UncheckedConsumeComponentValue();
  EXPECT_EQ(kLeftBraceToken, stream.Peek().GetType());
  stream.UncheckedConsumeComponentValue();
  EXPECT_EQ(kLeftBraceToken, stream.Peek().GetType());
  stream.UncheckedConsumeComponentValue();
  EXPECT_EQ(kIdentToken, stream.Peek().GetType());
  stream.UncheckedConsumeComponentValue();

  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, ConsumeWhitespace) {
  CSSTokenizer tokenizer(String(" \t\n"));  // kWhitespace
  CSSParserTokenStream stream(tokenizer);

  EXPECT_EQ(kWhitespaceToken, stream.Consume().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, ConsumeIncludingWhitespace) {
  CSSTokenizer tokenizer(String("A \t\n"));  // kIdent kWhitespace
  CSSParserTokenStream stream(tokenizer);

  EXPECT_EQ(kIdentToken, stream.ConsumeIncludingWhitespace().GetType());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, RangesDoNotGetInvalidatedWhenConsuming) {
  StringBuilder s;
  s.Append("1 ");
  for (int i = 0; i < 100; i++) {
    s.Append("A ");
  }

  CSSTokenizer tokenizer(s.ToString());
  CSSParserTokenStream stream(tokenizer);

  // Consume a single token range.
  auto range = stream.ConsumeUntilPeekedTypeIs<kIdentToken>();

  EXPECT_EQ(kNumberToken, range.Peek().GetType());

  // Consume remaining tokens to try to invalidate the range.
  while (!stream.AtEnd()) {
    stream.ConsumeIncludingWhitespace();
  }

  EXPECT_EQ(kNumberToken, range.ConsumeIncludingWhitespace().GetType());
  EXPECT_TRUE(range.AtEnd());
}

TEST(CSSParserTokenStreamTest, BlockErrorRecoveryConsumesRestOfBlock) {
  CSSTokenizer tokenizer(String("{B }1"));
  CSSParserTokenStream stream(tokenizer);

  {
    CSSParserTokenStream::BlockGuard guard(stream);
    EXPECT_EQ(kIdentToken, stream.Consume().GetType());
    EXPECT_FALSE(stream.AtEnd());
  }  // calls destructor

  EXPECT_EQ(kNumberToken, stream.Consume().GetType());
}

TEST(CSSParserTokenStreamTest, BlockErrorRecoveryOnSuccess) {
  CSSTokenizer tokenizer(String("{B }1"));
  CSSParserTokenStream stream(tokenizer);

  {
    CSSParserTokenStream::BlockGuard guard(stream);
    EXPECT_EQ(kIdentToken, stream.Consume().GetType());
    EXPECT_EQ(kWhitespaceToken, stream.Consume().GetType());
    EXPECT_TRUE(stream.AtEnd());
  }  // calls destructor

  EXPECT_EQ(kNumberToken, stream.Consume().GetType());
}

TEST(CSSParserTokenStreamTest, BlockErrorRecoveryConsumeComponentValue) {
  CSSTokenizer tokenizer(String("{{B} C}1"));
  CSSParserTokenStream stream(tokenizer);

  {
    CSSParserTokenStream::BlockGuard guard(stream);
    stream.EnsureLookAhead();
    stream.UncheckedConsumeComponentValue();
  }  // calls destructor

  EXPECT_EQ(kNumberToken, stream.Consume().GetType());
}

TEST(CSSParserTokenStreamTest, OffsetAfterPeek) {
  CSSTokenizer tokenizer(String("ABC"));
  CSSParserTokenStream stream(tokenizer);

  EXPECT_EQ(0U, stream.Offset());
  EXPECT_EQ(kIdentToken, stream.Peek().GetType());
  EXPECT_EQ(0U, stream.Offset());
}

TEST(CSSParserTokenStreamTest, OffsetAfterConsumes) {
  CSSTokenizer tokenizer(String("ABC 1 {23 }"));
  CSSParserTokenStream stream(tokenizer);

  EXPECT_EQ(0U, stream.Offset());
  EXPECT_EQ(kIdentToken, stream.Consume().GetType());
  EXPECT_EQ(3U, stream.Offset());
  EXPECT_EQ(kWhitespaceToken, stream.Consume().GetType());
  EXPECT_EQ(4U, stream.Offset());
  EXPECT_EQ(kNumberToken, stream.ConsumeIncludingWhitespace().GetType());
  EXPECT_EQ(6U, stream.Offset());
  stream.EnsureLookAhead();
  stream.UncheckedConsumeComponentValue();
  EXPECT_EQ(11U, stream.Offset());
}

TEST(CSSParserTokenStreamTest, LookAheadOffset) {
  CSSTokenizer tokenizer(String("ABC/* *//* */1"));
  CSSParserTokenStream stream(tokenizer);

  stream.EnsureLookAhead();
  EXPECT_EQ(0U, stream.Offset());
  EXPECT_EQ(0U, stream.LookAheadOffset());
  EXPECT_EQ(kIdentToken, stream.Consume().GetType());

  stream.EnsureLookAhead();
  EXPECT_EQ(3U, stream.Offset());
  EXPECT_EQ(13U, stream.LookAheadOffset());
}

TEST(CSSParserTokenStreamTest, ConsumeUntilPeekedTypeIsEmpty) {
  CSSTokenizer tokenizer(String("{23 }"));
  CSSParserTokenStream stream(tokenizer);

  auto range = stream.ConsumeUntilPeekedTypeIs<>();
  EXPECT_TRUE(stream.AtEnd());

  EXPECT_EQ(kLeftBraceToken, range.Consume().GetType());
  EXPECT_EQ(kNumberToken, range.Consume().GetType());
  EXPECT_EQ(kWhitespaceToken, range.Consume().GetType());
  EXPECT_EQ(kRightBraceToken, range.Consume().GetType());
  EXPECT_TRUE(range.AtEnd());
}

TEST(CSSParserTokenStreamTest, Boundary) {
  CSSTokenizer tokenizer(String("foo:red;bar:blue;asdf"));
  CSSParserTokenStream stream(tokenizer);

  {
    CSSParserTokenStream::Boundary boundary(stream, kSemicolonToken);
    CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
    EXPECT_EQ("foo", range.Consume().Value());
    EXPECT_EQ(kColonToken, range.Consume().GetType());
    EXPECT_EQ("red", range.Consume().Value());
    EXPECT_TRUE(stream.AtEnd());
  }

  EXPECT_FALSE(stream.AtEnd());
  EXPECT_EQ(kSemicolonToken, stream.Consume().GetType());

  {
    CSSParserTokenStream::Boundary boundary(stream, kSemicolonToken);
    CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
    EXPECT_EQ("bar", range.Consume().Value());
    EXPECT_EQ(kColonToken, range.Consume().GetType());
    EXPECT_EQ("blue", range.Consume().Value());
    EXPECT_TRUE(stream.AtEnd());
  }

  EXPECT_FALSE(stream.AtEnd());
  EXPECT_EQ(kSemicolonToken, stream.Consume().GetType());

  EXPECT_EQ("asdf", stream.Consume().Value());
  EXPECT_TRUE(stream.AtEnd());
}

TEST(CSSParserTokenStreamTest, MultipleBoundaries) {
  CSSTokenizer tokenizer(String("a:b,c;d:,;e"));
  CSSParserTokenStream stream(tokenizer);

  {
    CSSParserTokenStream::Boundary boundary_semicolon(stream, kSemicolonToken);

    {
      CSSParserTokenStream::Boundary boundary_comma(stream, kCommaToken);

      {
        CSSParserTokenStream::Boundary boundary_colon(stream, kColonToken);
        CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
        EXPECT_EQ("a", range.Consume().Value());
        EXPECT_TRUE(range.AtEnd());
        EXPECT_TRUE(stream.AtEnd());
      }

      EXPECT_FALSE(stream.AtEnd());
      EXPECT_EQ(kColonToken, stream.Consume().GetType());

      CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
      EXPECT_EQ("b", range.Consume().Value());
      EXPECT_TRUE(range.AtEnd());
      EXPECT_TRUE(stream.AtEnd());
    }

    EXPECT_FALSE(stream.AtEnd());
    EXPECT_EQ(kCommaToken, stream.Consume().GetType());

    CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
    EXPECT_EQ("c", range.Consume().Value());
    EXPECT_TRUE(range.AtEnd());
    EXPECT_TRUE(stream.AtEnd());
  }

  EXPECT_FALSE(stream.AtEnd());
  EXPECT_EQ(kSemicolonToken, stream.Consume().GetType());

  CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
  EXPECT_TRUE(stream.AtEnd());

  EXPECT_EQ("d", range.Consume().Value());
  EXPECT_EQ(kColonToken, range.Consume().GetType());
  EXPECT_EQ(kCommaToken, range.Consume().GetType());
  EXPECT_EQ(kSemicolonToken, range.Consume().GetType());
  EXPECT_EQ("e", range.Consume().Value());
}

TEST(CSSParserTokenStreamTest, IneffectiveBoundary) {
  CSSTokenizer tokenizer(String("a:b|"));
  CSSParserTokenStream stream(tokenizer);

  {
    CSSParserTokenStream::Boundary boundary_colon(stream, kColonToken);

    {
      // It's valid to add another boundary, but it has no affect in this
      // case, since kColonToken appears first.
      CSSParserTokenStream::Boundary boundary_semicolon(stream,
                                                        kSemicolonToken);

      CSSParserTokenRange range = stream.ConsumeUntilPeekedTypeIs<>();
      EXPECT_EQ("a", range.Consume().Value());
      EXPECT_TRUE(range.AtEnd());

      EXPECT_EQ(kColonToken, stream.Peek().GetType());
      EXPECT_TRUE(stream.AtEnd());
    }

    EXPECT_TRUE(stream.AtEnd());
  }

  EXPECT_FALSE(stream.AtEnd());
}

namespace {

Vector<CSSParserToken, 32> TokenizeAll(String string) {
  CSSTokenizer tokenizer(string);
  return tokenizer.TokenizeToEOF();
}

// See struct RestartData.
std::pair<wtf_size_t, wtf_size_t> ParseRestart(String restart) {
  wtf_size_t restart_target = restart.find('^');
  wtf_size_t restart_offset = restart.find('<');
  return std::make_pair(restart_target, restart_offset);
}

// Consume all tokens in `stream`, and store them in `tokens`,
// restarting (once) at the token with offset `restart_offset`
// to the offset specified by `restart_target`.
void TokenizeInto(CSSParserTokenStream& stream,
                  wtf_size_t restart_target,
                  wtf_size_t restart_offset,
                  Vector<CSSParserToken, 32>& tokens) {
  absl::optional<wtf_size_t> saved_offset;

  while (true) {
    stream.EnsureLookAhead();

    if (restart_target == stream.Offset()) {
      saved_offset = stream.Save();
    }

    if (saved_offset.has_value() && restart_offset == stream.Offset()) {
      stream.Restore(saved_offset.value());
      saved_offset.reset();
      // Do not restart again:
      restart_target = std::numeric_limits<wtf_size_t>::max();
      continue;
    }

    if (stream.AtEnd()) {
      return;
    }

    if (stream.UncheckedPeek().GetBlockType() == CSSParserToken::kBlockStart) {
      // Push block-start token about to be consumed by BlockGuard.
      tokens.push_back(stream.UncheckedPeek());
      CSSParserTokenStream::BlockGuard guard(stream);
      TokenizeInto(stream, restart_target, restart_offset, tokens);
      // Note that stream.AtEnd() is true for EOF, but also for
      // any block-end token.
      stream.EnsureLookAhead();
      DCHECK(stream.AtEnd());
      if (stream.UncheckedPeek().GetType() != kEOFToken) {
        // Add block-end token.
        tokens.push_back(stream.UncheckedPeek());
      }
    } else {
      tokens.push_back(stream.UncheckedConsume());
    }
  }
}

}  // namespace

struct RestartData {
  // The string to tokenize.
  const char* input;
  // Specifies where to restart from and to as follows:
  //
  // '^' - Restart to this offset.
  // '<' - Instead of consuming the token at this offset, restart to the
  //       offset indicated '^' instead.
  //
  // Example:
  //
  //  Input:   "foo bar baz"
  //  Restart: "    ^   <  "
  //
  // The above will consume foo, <space>, <bar>, <space>, then restart
  // at bar.
  //
  // Note that the '<' can appear at an offset equal to the length of the
  // input string, to represent restarts that happen when the stream is
  // at EOF.
  const char* restart;
  // Represents the expected token sequence, including the restart.
  // Continuing the example above, the appropriate 'ref' would be:
  //
  //  "foo bar bar baz"
  const char* ref;
};

RestartData restart_data[] = {
    // clang-format off
    {
      "x y z",
      "^ <  ",
      "x x y z"
    },
    {
      "x y z",
      "  ^ <",
      "x y y z"
    },
    {
      "x y z",
      "   ^<",
      "x y /**/ z"
    },
    {
      "x y z",
      "^<   ",
      "x/**/x y z"
    },
    {
      "x y z",
      "^  <",
      "x y/**/x y z"
    },

    // Restarting on block-start:
    {
      "x y { a b c } z",
      "  ^ <          ",
      "x y y { a b c } z"
    },
    {
      "x y ( a b c ) z",
      "  ^ <          ",
      "x y y ( a b c ) z"
    },
    {
      "x y { a b c } z",
      "  ^ <          ",
      "x y y { a b c } z"
    },
    {
      "x y foo( a b c ) z",
      "  ^ <          ",
      "x y y foo( a b c ) z"
    },

    // Restarting over a block:
    {
      "x y { a b c } z w",
      "  ^           <  ",
      "x y { a b c } y { a b c } z w"
    },
    {
      "x y { a b c } z w",
      "  ^          <   ",
      "x y { a b c }y { a b c } z w"
    },
    // Restart to block-start:
    {
      "x y { a b c } z w",
      "    ^         <   ",
      "x y { a b c } { a b c } z w"
    },

    // Restarting over an EOF-terminated block
    {
      "x y { a b c ",
      "  ^         <",
      "x y { a b c y { a b c "
    },

    // Restart within block:
    {
      "x y { a b c } z",
      "      ^   <    ",
      "x y { a b a b c } z"
    },
    {
      "x y { a b c } z",
      "     ^     <   ",
      "x y { a b c a b c } z"
    },
    {
      "x y { a b c } z",
      "     ^      <  ",
      "x y { a b c /**/ a b c } z"
    },
    // Restart within EOF-terminated block.
    {
      "x y {([ a b c d",
      "        ^   <  ",
      "x y {([ a b a b c d"
    },
    {
      "x y {([ a b c d",
      "        ^     <",
      "x y {([ a b c a b c d"
    },

    // clang-format on
};

class RestartTest : public testing::Test,
                    public testing::WithParamInterface<RestartData> {};

INSTANTIATE_TEST_SUITE_P(CSSParserTokenStreamTest,
                         RestartTest,
                         testing::ValuesIn(restart_data));

TEST_P(RestartTest, All) {
  RestartData param = GetParam();

  String ref(param.ref);
  Vector<CSSParserToken, 32> ref_tokens = TokenizeAll(ref);

  String input(param.input);
  CSSTokenizer tokenizer(input);
  CSSParserTokenStream stream(tokenizer);

  auto [restart_target, restart_offset] = ParseRestart(String(param.restart));
  Vector<CSSParserToken, 32> actual_tokens;
  TokenizeInto(stream, restart_target, restart_offset, actual_tokens);

  SCOPED_TRACE(testing::Message()
               << "Expected (serialized): "
               << CSSParserTokenRange(ref_tokens).Serialize());
  SCOPED_TRACE(testing::Message()
               << "Actual (serialized): "
               << CSSParserTokenRange(actual_tokens).Serialize());

  SCOPED_TRACE(param.ref);
  SCOPED_TRACE(param.restart);
  SCOPED_TRACE(param.input);

  EXPECT_EQ(actual_tokens, ref_tokens);
}

class NullRestartTest : public testing::Test,
                        public testing::WithParamInterface<RestartData> {};

INSTANTIATE_TEST_SUITE_P(CSSParserTokenStreamTest,
                         NullRestartTest,
                         testing::ValuesIn(restart_data));

// Ignores RestartData.restart, and instead tests restarting to and from
// the same offset, i.e. "restarting" to the offset we're already on.
TEST_P(NullRestartTest, All) {
  RestartData param = GetParam();

  String input(param.input);
  Vector<CSSParserToken, 32> ref_tokens = TokenizeAll(input);

  for (wtf_size_t restart_offset = 0; restart_offset <= input.length();
       ++restart_offset) {
    CSSTokenizer tokenizer(input);
    CSSParserTokenStream stream(tokenizer);

    Vector<CSSParserToken, 32> actual_tokens;
    TokenizeInto(stream, /* restart_target */ restart_offset, restart_offset,
                 actual_tokens);

    SCOPED_TRACE(testing::Message()
                 << "Expected (serialized): "
                 << CSSParserTokenRange(ref_tokens).Serialize());
    SCOPED_TRACE(testing::Message()
                 << "Actual (serialized): "
                 << CSSParserTokenRange(actual_tokens).Serialize());

    SCOPED_TRACE(param.input);
    SCOPED_TRACE(testing::Message() << "restart_offset:" << restart_offset);

    EXPECT_EQ(actual_tokens, ref_tokens);
  }
}

}  // namespace

}  // namespace blink
