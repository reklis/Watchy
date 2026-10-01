"""Exercise the actual editor/control loops with queued buttons, without hardware."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source_block(signature):
    text = (ROOT / "src/Watchy.cpp").read_text()
    start = text.index(signature)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


STUBS = r'''
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <cstddef>
constexpr int8_t CLOCK_BUTTON_ALERT = -2, CLOCK_BUTTON_TIMEOUT = -1;
constexpr int8_t CLOCK_BUTTON_MENU = 0, CLOCK_BUTTON_BACK = 1;
constexpr int8_t CLOCK_BUTTON_UP = 2, CLOCK_BUTTON_DOWN = 3;
struct Time { uint8_t Hour = 12, Minute = 0, Year = 55, Month = 3, Day = 14; };
struct { void read(Time &) {} } RTC;
bool clockToolsClockIsValid(const Time &) { return true; }
int tmYearToCalendar(int year) { return year + 1970; }
struct {
    void setFullWindow() {}
    void display(bool) {}
} display;
struct {
    void alarm(uint8_t, uint8_t, bool, uint8_t) {}
    void timerRunning(uint32_t) {}
    void timerEditor(uint8_t, uint8_t, uint8_t) {}
    void stopwatch(uint32_t, bool) {}
} clockToolsUI;
uint32_t epoch = 100000;
int saves = 0, backToMenu = 0, menuIndex = 6;
std::vector<int8_t> buttons;
std::size_t nextButton = 0;
std::vector<uint8_t> selections;
class Watchy {
public:
    Time currentTime;
    uint32_t _clockEpoch() { return epoch; }
    void _saveClockToolsState() { ++saves; }
    int8_t _waitForClockToolsButton() {
        assert(nextButton < buttons.size());
        return buttons[nextButton++];
    }
    void _drawClockToolsMenu(uint8_t selected, bool) { selections.push_back(selected); }
    void showMenu(int, bool) { ++backToMenu; }
    void _showClockTools();
    void _showAlarmEditor();
    void _showCountdownEditor();
    void _showStopwatch();
};
'''

CHECKS = r'''
void reset(std::initializer_list<int8_t> input) {
    clockToolsData = {};
    clockToolsData.alarmHour = 7;
    clockToolsData.alarmMinute = 30;
    clockToolsData.alarmEnabled = true;
    clockToolsData.countdownPresetMinutes = 25;
    saves = backToMenu = 0;
    epoch = 100000;
    buttons = input;
    nextButton = 0;
    selections.clear();
}
int main() {
    Watchy watch;
    reset({CLOCK_BUTTON_UP, CLOCK_BUTTON_MENU, CLOCK_BUTTON_DOWN,
           CLOCK_BUTTON_MENU, CLOCK_BUTTON_DOWN, CLOCK_BUTTON_MENU});
    watch._showAlarmEditor();
    assert(clockToolsData.alarmHour == 8 && clockToolsData.alarmMinute == 29);
    assert(!clockToolsData.alarmEnabled && saves == 1);

    // Both editors: UP increments, DOWN decrements, with independent field
    // wraparound at 23/0 hours and 59/0 minutes (no carry into another field).
    for (int8_t button : {CLOCK_BUTTON_UP, CLOCK_BUTTON_DOWN}) {
        const int delta = button == CLOCK_BUTTON_UP ? 1 : -1;
        for (int start : {0, 7, 23}) {
            const int expected = (start + delta + 24) % 24;
            reset({button, CLOCK_BUTTON_MENU, CLOCK_BUTTON_MENU, CLOCK_BUTTON_MENU});
            clockToolsData.alarmHour = start;
            watch._showAlarmEditor();
            assert(clockToolsData.alarmHour == expected && clockToolsData.alarmMinute == 30);
            assert(saves == 1);
            reset({button, CLOCK_BUTTON_MENU, CLOCK_BUTTON_MENU});
            clockToolsData.countdownPresetMinutes = start * 60 + 25;
            watch._showCountdownEditor();
            assert(clockToolsData.countdownPresetMinutes == expected * 60 + 25);
            assert(saves == 1);
        }
        for (int start : {0, 30, 59}) {
            const int expected = (start + delta + 60) % 60;
            reset({CLOCK_BUTTON_MENU, button, CLOCK_BUTTON_MENU, CLOCK_BUTTON_MENU});
            clockToolsData.alarmMinute = start;
            watch._showAlarmEditor();
            assert(clockToolsData.alarmMinute == expected && clockToolsData.alarmHour == 7);
            assert(saves == 1);
            reset({CLOCK_BUTTON_MENU, button, CLOCK_BUTTON_MENU});
            clockToolsData.countdownPresetMinutes = 60 + start;
            watch._showCountdownEditor();
            assert(clockToolsData.countdownPresetMinutes == 60 + expected);
            assert(saves == 1);
        }
    }

    for (int8_t exit : {CLOCK_BUTTON_BACK, CLOCK_BUTTON_TIMEOUT, CLOCK_BUTTON_ALERT}) {
        reset({CLOCK_BUTTON_DOWN, exit});
        watch._showAlarmEditor();
        assert(clockToolsData.alarmHour == 7 && clockToolsData.alarmMinute == 30);
        assert(clockToolsData.alarmEnabled && saves == 0);
        reset({CLOCK_BUTTON_DOWN, exit});
        watch._showCountdownEditor();
        assert(!clockToolsData.countdownActive && clockToolsData.countdownPresetMinutes == 25);
        assert(saves == 0);
    }

    reset({CLOCK_BUTTON_MENU, CLOCK_BUTTON_MENU});
    clockToolsData.countdownPresetMinutes = 0;
    watch._showCountdownEditor();
    assert(clockToolsData.countdownActive && clockToolsData.countdownPresetMinutes == 1);
    assert(clockToolsData.countdownEnd == epoch + 60 && saves == 1);

    reset({CLOCK_BUTTON_UP, CLOCK_BUTTON_MENU, CLOCK_BUTTON_UP, CLOCK_BUTTON_MENU});
    watch._showCountdownEditor();
    assert(clockToolsData.countdownPresetMinutes == 86);
    assert(clockToolsData.countdownEnd == epoch + 86 * 60 && saves == 1);

    reset({CLOCK_BUTTON_MENU});
    clockToolsData.countdownActive = true;
    clockToolsData.countdownEnd = epoch + 60;
    watch._showCountdownEditor();
    assert(!clockToolsData.countdownActive && clockToolsData.countdownEnd == 0 && saves == 1);
    reset({CLOCK_BUTTON_BACK});
    clockToolsData.countdownActive = true;
    clockToolsData.countdownEnd = epoch + 60;
    watch._showCountdownEditor();
    assert(clockToolsData.countdownActive && saves == 0);

    reset({CLOCK_BUTTON_MENU, CLOCK_BUTTON_BACK});
    clockToolsData.stopwatchElapsed = 600;
    watch._showStopwatch();
    assert(clockToolsData.stopwatchRunning && clockToolsData.stopwatchStarted == epoch);
    assert(clockToolsData.stopwatchElapsed == 600 && saves == 1);
    epoch += 75;
    buttons = {CLOCK_BUTTON_MENU, CLOCK_BUTTON_BACK}; nextButton = 0; saves = 0;
    watch._showStopwatch();
    assert(!clockToolsData.stopwatchRunning && clockToolsData.stopwatchStarted == 0);
    assert(clockToolsData.stopwatchElapsed == 675 && saves == 1);

    reset({CLOCK_BUTTON_DOWN, CLOCK_BUTTON_BACK});
    clockToolsData.stopwatchRunning = true;
    clockToolsData.stopwatchStarted = epoch - 60;
    clockToolsData.stopwatchElapsed = 600;
    watch._showStopwatch();
    assert(clockToolsData.stopwatchRunning && clockToolsData.stopwatchStarted == epoch);
    assert(clockToolsData.stopwatchElapsed == 0 && saves == 1);
    reset({CLOCK_BUTTON_DOWN, CLOCK_BUTTON_BACK});
    clockToolsData.stopwatchElapsed = 600;
    watch._showStopwatch();
    assert(!clockToolsData.stopwatchRunning && clockToolsData.stopwatchElapsed == 0 && saves == 1);

    reset({CLOCK_BUTTON_UP, CLOCK_BUTTON_DOWN, CLOCK_BUTTON_DOWN,
           CLOCK_BUTTON_MENU, CLOCK_BUTTON_BACK, CLOCK_BUTTON_BACK});
    watch._showClockTools();
    assert((selections == std::vector<uint8_t>{0, 2, 0, 1, 1}));
    assert(backToMenu == 1 && saves == 0);
    reset({CLOCK_BUTTON_ALERT});
    watch._showClockTools();
    assert(backToMenu == 0 && saves == 0);
}
'''


class ClockToolsControlTests(unittest.TestCase):
    def test_button_directions_wraparound_and_persistence(self):
        source = "\n".join([
            STUBS,
            source_block("struct ClockToolsData") + ";\nClockToolsData clockToolsData;",
            *[source_block(f"void Watchy::{method}()") for method in
              ("_showClockTools", "_showAlarmEditor", "_showCountdownEditor", "_showStopwatch")],
            CHECKS,
        ])
        with tempfile.TemporaryDirectory() as directory:
            cpp, binary = pathlib.Path(directory) / "controls.cpp", pathlib.Path(directory) / "controls"
            cpp.write_text(source)
            subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                            str(cpp), "-o", str(binary)],
                           check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
