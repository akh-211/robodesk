"""tools/behavior_log_report.py on a sample log in the RoboBehaviorLog.h formats."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import behavior_log_report as r  # noqa: E402

SAMPLE = """\
0.0 LEV,HEARTBEAT,uptime=1
0.0 LEV,BEH,SLEEP,from=sleep,to=awake
10.0 LEV,BEH,TOUCH,zone=head,latMs=40
10.1 LEV,BEH,REACT,id=head,var=101
20.0 LEV,BEH,TOUCH,zone=head,latMs=60
20.1 LEV,BEH,REACT,id=head,var=101
30.0 LEV,BEH,TOUCH,zone=side,latMs=120
30.1 LEV,BEH,REACT,id=side,var=110
40.0 LEV,BEH,IDLE,act=2,life=4
70.0 LEV,BEH,IDLE,act=2,life=4
100.0 LEV,BEH,IDLE,act=3,life=4
600.0 LEV,BEH,SOUND,cue=sleepy,spont=1
900.0 LEV,BEH,SOUND,cue=affection,spont=0
1200.0 LEV,BEH,SLEEP,from=awake,to=drowsy
1800.0 LEV,BEH,SOUND,cue=curious,spont=1
garbage LEV,BEH,TOUCH
3600.0 LEV,BEH,SLEEP,from=drowsy,to=lightsleep
"""


def main():
    out = r.report(r.parse(SAMPLE.splitlines()))
    assert out["span_s"] == 3600.0, out
    assert out["touch_samples"] == 3 and out["touch_p95_ms"] == 120 and out["touch_max_ms"] == 120, out
    assert out["reactions"] == 3 and out["reaction_repeats"] == 1, out
    assert out["idle_actions"] == 3 and out["idle_repeats"] == 1, out
    assert out["sounds"] == 3 and out["spontaneous_sounds"] == 2 and out["spontaneous_per_hour"] == 2.0, out
    assert out["sleep_state_s"] == {"awake": 1200.0, "drowsy": 2400.0, "lightsleep": 0.0}, out
    # Without the host timestamp only counts are available.
    bare = r.report(r.parse(["LEV,BEH,SOUND,cue=sleepy,spont=1", "LEV,BEH,TOUCH,zone=head,latMs=9"]))
    assert bare["span_s"] is None and bare["spontaneous_per_hour"] is None and bare["touch_p95_ms"] == 9, bare
    print("PASS: behavior_log_report sample")


if __name__ == "__main__":
    main()
