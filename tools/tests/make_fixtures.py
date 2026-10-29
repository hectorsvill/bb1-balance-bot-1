"""make_fixtures.py - write made-up BB1 logs for the tests, into a temp folder.

Logs never go in this public repo (they live in the private repo bb1-private),
so the tests can't use real ones. These are synthetic: the header lines have
the real layout (settings, not data), and every tick row is generated here,
just enough to exercise runs_db.py and logs.py:

  - a "wood fall" in log format 9: 60 ticks, armed until tick 47070, then the
    pitch passes 45 deg and BB1 is fallen (reason 2) from tick 47072
  - a "carpet drive" in log format 11: 60 ticks, a pause step then a
    "fwd 0.3" step, drive_speed 0.3 from tick 225670
plus a two-line flash history and a one-line command log.

    write_fixtures(folder) -> the log file names, oldest first
"""

import json
from pathlib import Path

TICKS = 60                 # rows in each log
MODE_ARMED, MODE_FALLEN = 4, 5

COLUMNS_9 = ("tick,t_us,work_us,mode,flag_bits,ax_raw,ay_raw,az_raw,temp_raw,gx_raw,gy_raw,gz_raw,pitch,"
             "pitch_rate,accel_pitch,enc_l,enc_r,speed_l,speed_r,pwm_l,pwm_r,setpoint,p_term,i_term,d_term,"
             "gains_ver,reason,web_age_ms,batt_v,auto_state,drive_speed,drive_turn")
COLUMNS_11 = COLUMNS_9 + ",drive_speed_now,drive_turn_now,speed_pwm"

HEADER_COMMON = """# alpha,0.980,loop_hz,200,accel_lsb_per_g,16384.0,gyro_lsb_per_dps,65.5
# accel_pitch_sign,1,gyro_pitch_sign,1
# counts_per_rev,520,speed_window_ticks,10,pwm_max,255,pwm_freq_hz,10000
# battery_divider,12.07,battery_pin,GP26,battery_cells,2
"""
DRIVE_LIMITS = "# drive_max_speed,1.00,drive_max_turn,1.00,drive_accel,1.00,drive_turn_accel,2.00,drive_turn_ff,15.0,drive_hold_ms,500\n"

FALL_NAME = "bb1_2026-01-01_10-00-00_ci-wood-fall.csv"
DRIVE_NAME = "bb1_2026-01-02_10-00-00_ci-carpet-drive.csv"
FALL_NOTE = "CI fixture: synthetic, 60 made-up ticks around a fall"
DRIVE_NOTE = "CI fixture: synthetic, 60 made-up ticks of a drive test"


def row(tick, t_us, mode, pitch, enc, speed, pwm, setpoint, reason, auto_state, drive, extra=()):
    """One tick as a CSV line. Only the columns the tools read carry meaning; the rest are plausible."""
    pitch_rate = 0.0 if mode != MODE_ARMED else round(pitch * 2, 3)
    values = [tick, t_us, 700, mode, 8 if mode == MODE_ARMED else 0,
              0, 0, 16384, -4000, 0, 0, 0,                      # raw IMU: flat and still
              f"{pitch:.3f}", f"{pitch_rate:.3f}", f"{pitch:.3f}",
              enc, enc, speed, speed, pwm, pwm, f"{setpoint:.2f}",
              f"{pwm * 0.5:.1f}", f"{pwm * 0.4:.1f}", f"{pwm * 0.1:.1f}",
              2, reason, 100, "7.90", auto_state, f"{drive:.2f}", "0.00", *extra]
    return ",".join(str(v) for v in values)


def fall_log():
    """Format 9: balancing, tipping forward, past 45 deg at tick 47071, fallen from then on."""
    lines = []
    for i in range(TICKS):
        tick = 47040 + i
        armed = tick <= 47070
        pitch = 1.5 * i if armed else 50.0                       # 0 .. 45 deg, then lying at 50
        lines.append(row(tick, 250_000_000 + i * 5000, MODE_ARMED if armed else MODE_FALLEN, pitch,
                         59000 + 10 * i, 800 if armed else 0, 150 if armed else 0, 0.0,
                         0 if tick < 47072 else 2, 6 if armed else 4, -1.0 if armed else 0.0))
    header = ("# firmware,bb1 milestone 2\n# log_format,9\n# gyro_bias_dps,-0.600,2.000,-2.100\n" + HEADER_COMMON +
              "# gains_ver,2,kp,18.000,ki,100.000,kd,0.500,trim,0.00,pwm_cap,200,deadband,10,speed_p,4.00,"
              "speed_i,0.50,turn_hold,20.0,auto_arm,1\n" + DRIVE_LIMITS +
              "# gains_v2,kp 18 ki 100 kd 0.5 trim 0 cap 200 deadband 10 sp 4 si 0.5 th 20 auto 1\n"
              f"# surface,wood\n# note,{FALL_NOTE}\n# recorded_at,2026-01-01T10:00:00-05:00\n"
              f"# records,{TICKS},lost,0\n")
    return header + COLUMNS_9 + "\n" + "\n".join(lines) + "\n"


def drive_log():
    """Format 11: standing still (pause step), then driving forward at 0.3 turns/s (fwd 0.3 step)."""
    lines = []
    for i in range(TICKS):
        tick = 225650 + i
        driving = tick >= 225670
        drive = 0.3 if driving else 0.0
        speed = 80 if driving else 0                              # counts/s
        lines.append(row(tick, 900_000_000 + i * 5000, MODE_ARMED, 0.5 if driving else 0.0,
                         1000 + (i - 20) * 2 if driving else 1000, speed, 30 if driving else 5,
                         0.8 if driving else 0.0, 0, 6, drive,
                         extra=(f"{drive:.2f}", "0.00", 0)))
    header = ("# firmware,bb1 milestone 2\n# firmware_version,3.2,build_id,ci synthetic\n# log_format,11\n"
              "# gyro_bias_dps,-0.600,2.040,-2.240\n" + HEADER_COMMON +
              "# gains_ver,2,kp,18.000,ki,100.000,kd,0.500,trim,0.00,pwm_cap,200,deadband,10,speed_p,3.00,"
              "speed_i,0.50,turn_hold,60.0,auto_arm,1,drive_ff,25,drive_speed_i,4.00\n" + DRIVE_LIMITS +
              "# step,pause,0.00,0.00,225650,225669\n# step,fwd 0.3,0.30,0.00,225670,225709\n"
              "# drive_test,fwd-back-30,done\n"
              "# gains_v2,kp 18 ki 100 kd 0.5 trim 0 cap 200 deadband 10 sp 3 si 0.5 th 60 auto 1 dff 25 sdi 4 "
              "smode 0 wsp 20 wsi 10 irelax 0.5\n"
              f"# surface,carpet\n# note,{DRIVE_NOTE}\n# recorded_at,2026-01-02T10:00:00-05:00\n"
              f"# records,{TICKS},lost,0\n")
    return header + COLUMNS_11 + "\n" + "\n".join(lines) + "\n"


def write_fixtures(folder):
    folder = Path(folder)
    (folder / FALL_NAME).write_text(fall_log())
    (folder / DRIVE_NAME).write_text(drive_log())
    (folder / "flash_history.csv").write_text("time,version,build_id,method,result\n"
                                              "2026-01-01 09:00:00,3.2,ci synthetic 1,wifi,ok\n"
                                              "2026-01-01 09:30:00,3.2,ci synthetic 2,wifi,ok\n")
    answer = json.dumps({"ok": True, "state": {"armed": True, "reason": ""}})
    (folder / "agent_commands.csv").write_text(
        "time,host,cmd,args,exit_code,answer\n"
        f'2026-01-02T10:00:00.100-05:00,bb1.local,state,"{{""full"": false}}",0,"{answer.replace(chr(34), chr(34) * 2)}"\n')
    return [FALL_NAME, DRIVE_NAME]
