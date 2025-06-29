// Copyright 2025 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ui/events/keycodes/keyboard_code_conversion_android.h"

#include <android/keycodes.h>

#include "testing/gtest/include/gtest/gtest.h"

namespace ui {
namespace {
struct KeyboardCodeConversionCobaltTestCase {
  std::string test_name;
  int input_keycode;
  KeyboardCode expected_value;
};

class KeyboardCodeConversionAndroidTest
    : public testing::TestWithParam<KeyboardCodeConversionCobaltTestCase> {};

TEST_P(KeyboardCodeConversionAndroidTest, TestKeyboardCodeConversion) {
  const KeyboardCodeConversionCobaltTestCase& test_case = GetParam();

  EXPECT_EQ(KeyboardCodeFromAndroidKeyCode(test_case.input_keycode),
            test_case.expected_value);
}

INSTANTIATE_TEST_SUITE_P(
    KeyboardCodeConversionCobaltTestSuiteInstantiation,
    KeyboardCodeConversionAndroidTest,
    testing::ValuesIn<KeyboardCodeConversionCobaltTestCase>(
        {{"AKEYCODE_DEL", AKEYCODE_DEL, VKEY_BACK},
         {"AKEYCODE_TAB", AKEYCODE_TAB, VKEY_TAB},
         {"AKEYCODE_CLEAR", AKEYCODE_CLEAR, VKEY_CLEAR},
         {"AKEYCODE_DPAD_CENTER", AKEYCODE_DPAD_CENTER, VKEY_RETURN},
         {"AKEYCODE_ENTER", AKEYCODE_ENTER, VKEY_RETURN},
         {"AKEYCODE_SHIFT_LEFT", AKEYCODE_SHIFT_LEFT, VKEY_LSHIFT},
         {"AKEYCODE_SHIFT_RIGHT", AKEYCODE_SHIFT_RIGHT, VKEY_RSHIFT},
         {"AKEYCODE_BACK", AKEYCODE_BACK, VKEY_BROWSER_BACK},
         {"AKEYCODE_FORWARD", AKEYCODE_FORWARD, VKEY_BROWSER_FORWARD},
         {"AKEYCODE_SPACE", AKEYCODE_SPACE, VKEY_SPACE},
         {"AKEYCODE_HOME", AKEYCODE_HOME, VKEY_HOME},
         {"AKEYCODE_DPAD_LEFT", AKEYCODE_DPAD_LEFT, VKEY_LEFT},
         {"AKEYCODE_DPAD_UP", AKEYCODE_DPAD_UP, VKEY_UP},
         {"AKEYCODE_DPAD_RIGHT", AKEYCODE_DPAD_RIGHT, VKEY_RIGHT},
         {"AKEYCODE_DPAD_DOWN", AKEYCODE_DPAD_DOWN, VKEY_DOWN},
         {"AKEYCODE_0", AKEYCODE_0, VKEY_0},
         {"AKEYCODE_1", AKEYCODE_1, VKEY_1},
         {"AKEYCODE_2", AKEYCODE_2, VKEY_2},
         {"AKEYCODE_3", AKEYCODE_3, VKEY_3},
         {"AKEYCODE_4", AKEYCODE_4, VKEY_4},
         {"AKEYCODE_5", AKEYCODE_5, VKEY_5},
         {"AKEYCODE_6", AKEYCODE_6, VKEY_6},
         {"AKEYCODE_7", AKEYCODE_7, VKEY_7},
         {"AKEYCODE_8", AKEYCODE_8, VKEY_8},
         {"AKEYCODE_9", AKEYCODE_9, VKEY_9},
         {"AKEYCODE_A", AKEYCODE_A, VKEY_A},
         {"AKEYCODE_B", AKEYCODE_B, VKEY_B},
         {"AKEYCODE_C", AKEYCODE_C, VKEY_C},
         {"AKEYCODE_D", AKEYCODE_D, VKEY_D},
         {"AKEYCODE_E", AKEYCODE_E, VKEY_E},
         {"AKEYCODE_F", AKEYCODE_F, VKEY_F},
         {"AKEYCODE_G", AKEYCODE_G, VKEY_G},
         {"AKEYCODE_H", AKEYCODE_H, VKEY_H},
         {"AKEYCODE_I", AKEYCODE_I, VKEY_I},
         {"AKEYCODE_J", AKEYCODE_J, VKEY_J},
         {"AKEYCODE_K", AKEYCODE_K, VKEY_K},
         {"AKEYCODE_L", AKEYCODE_L, VKEY_L},
         {"AKEYCODE_M", AKEYCODE_M, VKEY_M},
         {"AKEYCODE_N", AKEYCODE_N, VKEY_N},
         {"AKEYCODE_O", AKEYCODE_O, VKEY_O},
         {"AKEYCODE_P", AKEYCODE_P, VKEY_P},
         {"AKEYCODE_Q", AKEYCODE_Q, VKEY_Q},
         {"AKEYCODE_R", AKEYCODE_R, VKEY_R},
         {"AKEYCODE_S", AKEYCODE_S, VKEY_S},
         {"AKEYCODE_T", AKEYCODE_T, VKEY_T},
         {"AKEYCODE_U", AKEYCODE_U, VKEY_U},
         {"AKEYCODE_V", AKEYCODE_V, VKEY_V},
         {"AKEYCODE_W", AKEYCODE_W, VKEY_W},
         {"AKEYCODE_X", AKEYCODE_X, VKEY_X},
         {"AKEYCODE_Y", AKEYCODE_Y, VKEY_Y},
         {"AKEYCODE_Z", AKEYCODE_Z, VKEY_Z},
         {"AKEYCODE_VOLUME_DOWN", AKEYCODE_VOLUME_DOWN, VKEY_VOLUME_DOWN},
         {"AKEYCODE_VOLUME_UP", AKEYCODE_VOLUME_UP, VKEY_VOLUME_UP},
         {"AKEYCODE_MEDIA_NEXT", AKEYCODE_MEDIA_NEXT, VKEY_MEDIA_NEXT_TRACK},
         {"AKEYCODE_MEDIA_PREVIOUS", AKEYCODE_MEDIA_PREVIOUS,
          VKEY_MEDIA_PREV_TRACK},
         {"AKEYCODE_MEDIA_STOP", AKEYCODE_MEDIA_STOP, VKEY_MEDIA_STOP},
         {"AKEYCODE_MEDIA_PAUSE", AKEYCODE_MEDIA_PAUSE, VKEY_MEDIA_PLAY_PAUSE},
         {"AKEYCODE_SEMICOLON", AKEYCODE_SEMICOLON, VKEY_OEM_1},
         {"AKEYCODE_COMMA", AKEYCODE_COMMA, VKEY_OEM_COMMA},
         {"AKEYCODE_MINUS", AKEYCODE_MINUS, VKEY_OEM_MINUS},
         {"AKEYCODE_EQUALS", AKEYCODE_EQUALS, VKEY_OEM_PLUS},
         {"AKEYCODE_PERIOD", AKEYCODE_PERIOD, VKEY_OEM_PERIOD},
         {"AKEYCODE_SLASH", AKEYCODE_SLASH, VKEY_OEM_2},
         {"AKEYCODE_LEFT_BRACKET", AKEYCODE_LEFT_BRACKET, VKEY_OEM_4},
         {"AKEYCODE_BACKSLASH", AKEYCODE_BACKSLASH, VKEY_OEM_5},
         {"AKEYCODE_RIGHT_BRACKET", AKEYCODE_RIGHT_BRACKET, VKEY_OEM_6},
         {"AKEYCODE_MUTE", AKEYCODE_MUTE, VKEY_VOLUME_MUTE},
         {"AKEYCODE_VOLUME_MUTE", AKEYCODE_VOLUME_MUTE, VKEY_VOLUME_MUTE},
         {"AKEYCODE_ESCAPE", AKEYCODE_ESCAPE, VKEY_ESCAPE},
         {"AKEYCODE_MEDIA_PLAY", AKEYCODE_MEDIA_PLAY, VKEY_MEDIA_PLAY_PAUSE},
         {"AKEYCODE_MEDIA_PLAY_PAUSE", AKEYCODE_MEDIA_PLAY_PAUSE,
          VKEY_MEDIA_PLAY_PAUSE},
         {"AKEYCODE_MOVE_END", AKEYCODE_MOVE_END, VKEY_END},
         {"AKEYCODE_ALT_LEFT", AKEYCODE_ALT_LEFT, VKEY_LMENU},
         {"AKEYCODE_ALT_RIGHT", AKEYCODE_ALT_RIGHT, VKEY_RMENU},
         {"AKEYCODE_GRAVE", AKEYCODE_GRAVE, VKEY_OEM_3},
         {"AKEYCODE_APOSTROPHE", AKEYCODE_APOSTROPHE, VKEY_OEM_3},
         {"AKEYCODE_MEDIA_REWIND", AKEYCODE_MEDIA_REWIND, VKEY_OEM_103},
         {"AKEYCODE_MEDIA_FAST_FORWARD", AKEYCODE_MEDIA_FAST_FORWARD,
          VKEY_OEM_104},
         {"AKEYCODE_PAGE_UP", AKEYCODE_PAGE_UP, VKEY_PRIOR},
         {"AKEYCODE_PAGE_DOWN", AKEYCODE_PAGE_DOWN, VKEY_NEXT},
         {"AKEYCODE_FORWARD_DEL", AKEYCODE_FORWARD_DEL, VKEY_DELETE},
         {"AKEYCODE_CTRL_LEFT", AKEYCODE_CTRL_LEFT, VKEY_LCONTROL},
         {"AKEYCODE_CTRL_RIGHT", AKEYCODE_CTRL_RIGHT, VKEY_RCONTROL},
         {"AKEYCODE_CAPS_LOCK", AKEYCODE_CAPS_LOCK, VKEY_CAPITAL},
         {"AKEYCODE_SCROLL_LOCK", AKEYCODE_SCROLL_LOCK, VKEY_SCROLL},
         {"AKEYCODE_META_LEFT", AKEYCODE_META_LEFT, VKEY_LWIN},
         {"AKEYCODE_META_RIGHT", AKEYCODE_META_RIGHT, VKEY_RWIN},
         {"AKEYCODE_BREAK", AKEYCODE_BREAK, VKEY_PAUSE},
         {"AKEYCODE_INSERT", AKEYCODE_INSERT, VKEY_INSERT},
         {"AKEYCODE_F1", AKEYCODE_F1, VKEY_F1},
         {"AKEYCODE_F2", AKEYCODE_F2, VKEY_F2},
         {"AKEYCODE_F3", AKEYCODE_F3, VKEY_F3},
         {"AKEYCODE_F4", AKEYCODE_F4, VKEY_F4},
         {"AKEYCODE_F5", AKEYCODE_F5, VKEY_F5},
         {"AKEYCODE_F6", AKEYCODE_F6, VKEY_F6},
         {"AKEYCODE_F7", AKEYCODE_F7, VKEY_F7},
         {"AKEYCODE_F8", AKEYCODE_F8, VKEY_F8},
         {"AKEYCODE_F9", AKEYCODE_F9, VKEY_F9},
         {"AKEYCODE_F10", AKEYCODE_F10, VKEY_F10},
         {"AKEYCODE_F11", AKEYCODE_F11, VKEY_F11},
         {"AKEYCODE_F12", AKEYCODE_F12, VKEY_F12},
         {"AKEYCODE_NUM_LOCK", AKEYCODE_NUM_LOCK, VKEY_NUMLOCK},
         {"AKEYCODE_NUMPAD_0", AKEYCODE_NUMPAD_0, VKEY_NUMPAD0},
         {"AKEYCODE_NUMPAD_1", AKEYCODE_NUMPAD_1, VKEY_NUMPAD1},
         {"AKEYCODE_NUMPAD_2", AKEYCODE_NUMPAD_2, VKEY_NUMPAD2},
         {"AKEYCODE_NUMPAD_3", AKEYCODE_NUMPAD_3, VKEY_NUMPAD3},
         {"AKEYCODE_NUMPAD_4", AKEYCODE_NUMPAD_4, VKEY_NUMPAD4},
         {"AKEYCODE_NUMPAD_5", AKEYCODE_NUMPAD_5, VKEY_NUMPAD5},
         {"AKEYCODE_NUMPAD_6", AKEYCODE_NUMPAD_6, VKEY_NUMPAD6},
         {"AKEYCODE_NUMPAD_7", AKEYCODE_NUMPAD_7, VKEY_NUMPAD7},
         {"AKEYCODE_NUMPAD_8", AKEYCODE_NUMPAD_8, VKEY_NUMPAD8},
         {"AKEYCODE_NUMPAD_9", AKEYCODE_NUMPAD_9, VKEY_NUMPAD9},
         {"AKEYCODE_NUMPAD_DIVIDE", AKEYCODE_NUMPAD_DIVIDE, VKEY_DIVIDE},
         {"AKEYCODE_NUMPAD_MULTIPLY", AKEYCODE_NUMPAD_MULTIPLY, VKEY_MULTIPLY},
         {"AKEYCODE_NUMPAD_SUBTRACT", AKEYCODE_NUMPAD_SUBTRACT, VKEY_SUBTRACT},
         {"AKEYCODE_NUMPAD_ADD", AKEYCODE_NUMPAD_ADD, VKEY_ADD},
         {"AKEYCODE_NUMPAD_DOT", AKEYCODE_NUMPAD_DOT, VKEY_DECIMAL},
         {"AKEYCODE_CHANNEL_UP", AKEYCODE_CHANNEL_UP, VKEY_PRIOR},
         {"AKEYCODE_CHANNEL_DOWN", AKEYCODE_CHANNEL_DOWN, VKEY_NEXT},
         {"AKEYCODE_CAPTIONS", AKEYCODE_CAPTIONS, KEY_SUBTITLES},
         {"AKEYCODE_SEARCH", AKEYCODE_SEARCH, VKEY_BROWSER_SEARCH},
         // Test that an unmapped key code returns VKEY_UNKNOWN
         {"AKEYCODE_UNKNOWN", AKEYCODE_UNKNOWN, VKEY_UNKNOWN}}),
    [](const testing::TestParamInfo<
        KeyboardCodeConversionAndroidTest::ParamType>& info) {
      return info.param.test_name;
    });
}  // namespace
}  // namespace ui
