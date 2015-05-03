#!/bin/sh

# Examples from EN 300 231 Annex E.3.

set -e    # abort on errors for make check
opts="-t" # pdc2: -t test mode

# reNM: Isolate the Nth of M VCR states, except in comment lines.
re12='/.*#.*/!s/\([A-Z]*\) *\([A-Z]*\) *$/\1/'
re22='/.*#.*/!s/\([A-Z]*\) *\([A-Z]*\) *$/\2/'
re13='/.*#.*/!s/\([A-Z]*\) *\([A-Z]*\) *\([A-Z]*\) *$/\1/'
re23='/.*#.*/!s/\([A-Z]*\) *\([A-Z]*\) *\([A-Z]*\) *$/\2/'
re33='/.*#.*/!s/\([A-Z]*\) *\([A-Z]*\) *\([A-Z]*\) *$/\3/'

example1() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    BBC1 19790701T191410 0 0 0 0 0701T1840   BBC1 REC  SCAN
    BBC1 19790701T191420 0 0 0 x RI/T        BBC1 REC  SCAN
    BBC1 19790701T191445 0 0 0 1 0701T1915/N BBC1 REC  PTR
    BBC1 19790701T191450 0 0 0 1 0701T1915/N BBC1 SCAN PTR
    BBC1 19790701T191453 0 0 0 1 0701T1915/N BBC1 SCAN PTR
    BBC1 19790701T191515 0 0 0 0 0701T1915/N BBC1 SCAN REC
    BBC1 19790701T194235 0 0 0 x RI/T        BBC1 SCAN REC
    BBC1 19790701T194305 0 0 0 x RI/T        BBC1 SCAN STBY
    BBC1 19790701T194319 0 0 0 x RI/T        BBC1 SCAN STBY
    BBC1 19790701T194330 0 0 0 1 0701T1945   BBC1 PTR  STBY
    BBC1 19790701T194400 0 0 0 0 0701T1945   BBC1 REC  STBY
  `

  # Start time, end time and VPS/PDC time are identical because
  # EN 300 231 gives no end time (pdc2 interprets start == end as
  # duration unknown) and the VPS/PDC time is equal to the start
  # time in Example 1.
  echo "EN 300 231 Annex E.3 Example 1, VCR 1"
  echo "$table" | sed "$re12" | ./pdc2 $opts \
    1979-07-01 18:40 18:40 18:40 \
    1979-07-01 19:45 19:45 19:45

  echo "EN 300 231 Annex E.3 Example 1, VCR 2"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-01 19:15 19:15 19:15
}

example2() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    BBC1 19790701T191410 0 0 1 0 0701T1840   BBC1 REC  SCAN

    BBC1 19790701T191420 0 0 1 0 0701T1840   BBC1 REC  SCAN

    BBC1 19790701T191445 0 0 1 0 0701T1840   BBC1
    BBC1 19790701T191445 1 0 1 1 0701T1915/N BBC1 REC  PTR

    BBC1 19790701T191450 0
    BBC1 19790701T191450 1 0 1 1 0701T1915/N BBC1 REC  PTR
    # The VCR 1 REC state at 191450ff seems to be wrong. Table E.3
    # states: "The 1840 label is no longer present, and another valid
    # label is present, so VCRs responsive to MI and which were
    # recording against the 1840 label will now stop. Other VCRs will
    # continue recording for 30 s." Accordingly the state should be
    # REC (ignoring MI) or SCAN (observing MI, as pdc2 does) between
    # 191450 and 191520, and SCAN between 191520 and 194330.

    BBC1 19790701T191451 1 0 1 1 0701T1915/N BBC1 SCAN PTR
    # Since EN 300 231 does not require PIDs with different LCIs to be
    # transmitted in any particular order pdc2 will not acknowledge
    # the absence of 0701T1840 on channel 0 until the label was
    # missing twice. This line is not in Table E.4.

    # BBC1 19790701T191453 1 0 1 1 0701T1915/N BBC1 REC  PTR
    BBC1 19790701T191453 1 0 1 1 0701T1915/N BBC1 SCAN PTR
    # See first comment above.

    BBC1 19790701T191515 1 0 1 0 0701T1915/N BBC1 SCAN REC

    BBC1 19790701T194235 1 0 1 0 0701T1915/N BBC1 SCAN REC

    BBC1 19790701T194305 0 0 1 x RI/T        BBC1
    # BBC1 19790701T194305 1                        SCAN STBY
    BBC1 19790701T194305 1                        SCAN REC
    # See next comment.

    BBC1 19790701T194306 0 0 1 x RI/T        BBC1 SCAN STBY
    # As above pdc2 needs one cycle to acknowledge the absence
    # of 0701T1915/N on channel 1. This line is not in Table E.4.

    BBC1 19790701T194319 0 0 1 x RI/T        BBC1 SCAN STBY

    BBC1 19790701T194330 0 0 1 1 0701T1945   BBC1 PTR  STBY

    BBC1 19790701T194400 0 0 1 0 0701T1945   BBC1 REC  STBY
  `

  echo "EN 300 231 Annex E.3 Example 2, VCR 1 resp. to MI"
  echo "$table" | sed "$re12" | ./pdc2 $opts \
    1979-07-01 18:40 18:40 18:40 \
    1979-07-01 19:45 19:45 19:45

  echo "EN 300 231 Annex E.3 Example 2, VCR 2 resp. to MI"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-01 19:15 19:15 19:15
}

example3() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790702T123456 0 0 0 0 0702T1200 ITV1 REC  SCAN

    ITV1 19790702T123530 0 0 0 1 0702T1235 ITV1 REC  PTR

    ITV1 19790702T123550 0 0 0 x INT       ITV1
    ITV1 19790702T123550 1 0 0 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123600 0
    ITV1 19790702T123600 1 0 0 1 NSPV/3F   ITV1 PTR  SCAN

    ITV1 19790702T123620 1 0 0 0 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123715 0 0 0 0 0702T1235 ITV1
    ITV1 19790702T123715 1                      REC  REC

    ITV1 19790702T123720 0 0 0 0 0702T1235 ITV1 REC  REC

    ITV1 19790702T123745 0 0 0 0 0702T1235 ITV1 SCAN REC
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
  # EN 300 231 does not specify a start time for the second program
  # which is needed to calculate the PTY validity window, so we
  # use 00:00.
#  echo "EN 300 231 Annex E.3 Example 3, VCR 1:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 12:00 12:00 12:00 \
#    1979-07-02 00:00 00:00 3F

  echo "EN 300 231 Annex E.3 Example 3, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 12:35 12:35 12:35
}

example4a() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790702T123456 0 0 1 0 0702T1200 ITV1 REC  SCAN

    ITV1 19790702T123530 0 0 1 0 0702T1200 ITV1
    ITV1 19790702T123530 1 0 1 1 0702T1235 ITV1 REC  PTR

    ITV1 19790702T123550 0 0 1 0 0702T1200 ITV1
    ITV1 19790702T123550 1 0 1 x INT       ITV1
    ITV1 19790702T123550 2 0 1 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123555 0 0 1 0 0702T1200 ITV1
    ITV1 19790702T123555 1
    ITV1 19790702T123555 2 0 1 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123600 0
    ITV1 19790702T123600 2 0 1 0 NSPV/3F   ITV1 REC  SCAN
    # As in Example 2 pdc2 needs one cycle to acknowledge the absence
    # of 0702T1200 on channel 0, so VCR1 will actually stop recording
    # the 1200 program and start recording the NSPV/3F program at
    # 123601.

    ITV1 19790702T123620 2 0 1 0 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123715 0 0 1 1 0702T1235 ITV1
    # ITV1 19790702T123715 2                      REC  PTR
    ITV1 19790702T123715 2                      SCAN PTR
    # The VCR 1 REC state at 123715ff seems to be wrong because the
    # label NSPV/3F with MI=1 disappeared. Table E.7 states: "30 s cue
    # is not available for the end of the Newsflash, so VCRs not
    # responsive to MI, and which were recording against PTY = 3F will
    # continue to do so for 30 s into the following announcement."
    # Observing MI the VCR 1 state should change to SCAN because MI=1
    # stops recording immediately and a program selected by PTY is not
    # to be deleted from the schedule.

    # ITV1 19790702T123720 0 0 1 1 0702T1235 ITV1 REC  PTR
    ITV1 19790702T123720 0 0 1 1 0702T1235 ITV1 SCAN PTR
    # See previous comment.

    ITV1 19790702T123745 0 0 1 0 0702T1235 ITV1 SCAN REC
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 4, VCR 1 resp. to MI:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 12:00 12:00 12:00 \
#    1979-07-02 00:00 00:00 3F

  echo "EN 300 231 Annex E.3 Example 4, VCR 2 resp. to MI:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 12:35 12:35 12:35
}

example4b() {
  # The VCR states in Example 4 seem to describe VCRs not responsive
  # to MI, so let's try Example 4 with MI = 0 throughout.

  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790702T123456 0 0 0 0 0702T1200 ITV1 REC  SCAN

    ITV1 19790702T123530 0 0 0 0 0702T1200 ITV1
    ITV1 19790702T123530 1 0 0 1 0702T1235 ITV1 REC  PTR

    ITV1 19790702T123550 0 0 0 0 0702T1200 ITV1
    ITV1 19790702T123550 1 0 0 x INT       ITV1
    ITV1 19790702T123550 2 0 0 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123555 0 0 0 0 0702T1200 ITV1
    ITV1 19790702T123555 1
    ITV1 19790702T123555 2 0 0 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123600 0
    ITV1 19790702T123600 2 0 0 0 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123620 2 0 0 0 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123715 0 0 0 1 0702T1235 ITV1
    ITV1 19790702T123715 2                      REC  PTR

    ITV1 19790702T123720 0 0 0 1 0702T1235 ITV1 REC  PTR

    ITV1 19790702T123745 0 0 0 0 0702T1235 ITV1 SCAN REC
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 4, VCR 1 not resp. to MI:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 12:00 12:00 12:00 \
#    1979-07-02 00:00 00:00 3F

  echo "EN 300 231 Annex E.3 Example 4, VCR 2 not resp. to MI:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 12:35 12:35 12:35
}

example5() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790702T123456 0 0 0 0 0702T1200 ITV1 REC  SCAN

    ITV1 19790702T123530 0 0 0 x INT       ITV1
    ITV1 19790702T123530 1 0 0 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123550 0
    ITV1 19790702T123550 1 0 0 1 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123600 1 0 0 0 NSPV/3F   ITV1 REC  SCAN

    ITV1 19790702T123715 1 0 0 1 0702T1235 ITV1 REC  PTR

    ITV1 19790702T123720 1 0 0 1 0702T1235 ITV1 REC  PTR

    ITV1 19790702T123745 1 0 0 0 0702T1235 ITV1 SCAN REC

    ITV1 19790702T123850 0 0 0 1 0702T1200 ITV1
    ITV1 19790702T123850 1 0 0 0 0702T1235 ITV1 PTR  REC

    ITV1 19790702T123900 0
    ITV1 19790702T123900 1 0 0 0 0702T1235 ITV1 PTR  REC
    # The VCR 1 PTR state at 123900 seems to be wrong because the
    # label 1200 with PRF=1 disappeared and another valid label is
    # present. MI was and is 0, but why would VCR 1 delay a PTR ->
    # SCAN change for 30 seconds?

    ITV1 19790702T123931 1 0 0 0 0702T1235 ITV1 SCAN REC
    # As in Example 2 pdc2 needs one cycle to acknowledge the absence
    # of 0702T1200 on channel 0. This line is not in Table E.10.

    ITV1 19790702T123930 1 0 0 0 0702T1235 ITV1 SCAN REC
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 5, VCR 1:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 12:00 12:00 12:00 \
#    1979-07-02 00:00 00:00 3F

  echo "EN 300 231 Annex E.3 Example 5, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 12:35 12:35 12:35
}

example6() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790702T123456 0 0 1 0 0702T1200 ITV1 REC  SCAN
    ITV1 19790702T123530 0 0 1 1 NSPV/3F   ITV1 PTR  SCAN
    ITV1 19790702T123535 0 0 1 1 NSPV/3F   ITV1 PTR  SCAN
    ITV1 19790702T123600 0 0 1 0 NSPV/3F   ITV1 REC  SCAN
    ITV1 19790702T123715 0 0 1 1 0702T1235 ITV1 SCAN PTR
    ITV1 19790702T123720 0 0 1 1 0702T1235 ITV1 SCAN PTR
    ITV1 19790702T123745 0 0 1 0 0702T1235 ITV1 SCAN REC
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 6, VCR 1:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 12:00 12:00 12:00 \
#    1979-07-02 00:00 00:00 3F

  echo "EN 300 231 Annex E.3 Example 6, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 12:35 12:35 12:35
}

example7() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    CHL4 19790702T140000 0 0 0 0 0702T1400 CHL4 REC  STBY

    CHL4 19790702T140404 0 0 0 0 0702T1400 CHL4 REC  STBY

    CHL4 19790702T140418 0 0 1 x INT       CHL4 SCAN STBY
    # Note Table E.13: "Those VCRs which were recording the film will
    # now enter "Pause" mode immediately if they are responsive to MI,
    # or after 30 s if they are not responsive to MI."

    CHL4 19790702T140650 0 0 0 1 0702T1400 CHL4 PTR  STBY

    CHL4 19790702T140700 0 0 0 0 0702T1400 CHL4 REC  STBY
  `

  echo "EN 300 231 Annex E.3 Example 7, VCR 1:"
  echo "$table" | sed "$re12" | ./pdc2 $opts \
    1979-07-02 14:00 14:00 14:00

  echo "EN 300 231 Annex E.3 Example 7, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts
}

example8() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    BBC2 19790702T182200 0 0 1 0 0702T1620/T BBC2 REC  SCAN

    BBC2 19790702T183040 0 1 x x 0702T1829/T BBC1
    # BBC2 19790702T183040 1 0 1 1 0702T1830   BBC2 REC  PTR
    BBC2 19790702T183040 1 0 1 1 0702T1830   BBC2 STBY PTR
    # The VCR 1 REC state at 183040 seems to be wrong because the
    # label 1829 with LUF=1 immediately terminates the label 1620 with
    # MI=1. Table E.15 states: "VCRs responsive to MI will stop
    # recording, store the new PIL and begin scanning within (say) 5
    # s." However since pdc2 does not support VCR reprogramming it
    # stops recording, removes the program from the schedule and
    # changes to STBY state.

    BBC2 19790702T183110 0
    # BBC2 19790702T183110 1 0 1 0 0702T1830   BBC2 REC  REC
    BBC2 19790702T183110 1 0 1 0 0702T1830   BBC2 STBY REC
    # The VCR 1 REC state at 183110 seems to be wrong because the
    # label 1620 with MI=1 disappeared at 183040 and a new valid PIL
    # with PRF=0 is present. Table E.16 makes sense only if we assume
    # VCR 1 ignores MI and the LUF=1 label, as if label 1620 with MI=0
    # was present until 183109.

    # BBC2 19790702T183140 1 0 1 0 0702T1830   BBC2 SCAN REC
    BBC2 19790702T183140 1 0 1 0 0702T1830   BBC2 STBY REC
    # The VCR 1 SCAN state at 183140 is correct, but pdc2 does not
    # support VCR reprogramming and its schedule is empty by now.
  `

  echo "EN 300 231 Annex E.3 Example 8, VCR 1:"
  echo "$table" | sed "$re12" | ./pdc2 $opts \
    1979-07-02 16:20 16:20 16:20

  echo "EN 300 231 Annex E.3 Example 8, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 18:30 18:30 18:30
}

example9() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790702T210000 0 0 1 x RI/T        ITV1 SCAN SCAN

    ITV1 19790702T210005 0 0 0 0 0702T2100/Q ITV1 REC  SCAN

    ITV1 19790702T210035 0 1 x x 0703T2229/Q ITV1 REC  SCAN

    # ITV1 19790702T210045 0 0 1 x RI/T        ITV1 SCAN SCAN
    ITV1 19790702T210045 0 0 1 x RI/T        ITV1 REC  SCAN
    # The VCR 1 SCAN state at 210045 seems to be wrong. Table E.17
    # states about 210035: "Label of the rescheduled programme for
    # reprogramming of VCR memories, with LUF = 1, and where the label
    # 02JY2100 which has just been removed from label channel 0 is
    # replaced. VCRs which were pre-programmed with the 2100 or the
    # Series code "Q" will continue to record for 30 s, and so record
    # the announcement." This is consistent with MI=0. So VCR 1 should
    # record until 210105, then change to STBY state as all scheduled
    # programs now start on 19790703.

    # ITV1 19790702T210055 0 0 0 1 0702T2059   ITV1 SCAN PTR
    ITV1 19790702T210055 0 0 0 1 0702T2059   ITV1 REC  PTR

    # ITV1 19790702T210105 0 0 0 1 0702T2059   ITV1 STBY PTR
    # This line is not in Table E.17.

    # ITV1 19790702T210115 0 0 0 0 0702T2059   ITV1 SCAN REC
    ITV1 19790702T210115 0 0 0 0 0702T2059   ITV1 STBY REC

    # ITV1 19790702T210200 0 0 1 0 0702T2059   ITV1 SCAN REC
    ITV1 19790702T210200 0 0 1 0 0702T2059   ITV1 STBY REC

    # ITV1 19790702T214500 0 0 1 0 RI/T        ITV1 SCAN STBY
    ITV1 19790702T214500 0 0 1 0 RI/T        ITV1 STBY STBY

    ITV1 19790703T000000 0 0 1 0 RI/T        ITV1 SCAN SCAN
    # Table E.18 does not show the VCR 1 and 2 state change from STBY
    # to SCAN at 19790703T0000 as we enter the PIL validity window of
    # the programs scheduled for 19790703.

    ITV1 19790703T200000 0 0 1 0 0703T2001   ITV1 REC  SCAN

    ITV1 19790703T223020 0 0 1 1 0703T2230   ITV1 SCAN PTR

    # ITV1 19790703T223050 0 0 1 1 0703T2229/Q ITV1 PTR  STBY
    ITV1 19790703T223050 0 0 1 1 0703T2229/Q ITV1 PTR  SCAN
    # The VCR 2 STBY state at 19790703T223050 seems to be wrong. Why
    # should we remove the 2nd program from the schedule here?

    # ITV1 19790703T223120 0 0 1 0 0703T2229/Q ITV1 REC  STBY
    ITV1 19790703T223120 0 0 1 0 0703T2229/Q ITV1 REC  SCAN

    # ITV1 19790703T225500 0 0 1 0 0703T2229/Q ITV1 REC  STBY
    ITV1 19790703T225500 0 0 1 0 0703T2229/Q ITV1 REC  SCAN
  `

  # Test omitted because ./pdc2 does not support VCR reprogramming.
#  echo "EN 300 231 Annex E.3 Example 9, VCR 1:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 21:00 21:00 21:00 \
#    1979-07-03 20:01 20:01 20:01

  echo "EN 300 231 Annex E.3 Example 9, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 20:59 20:59 20:59 \
    1979-07-03 22:30 22:30 22:30
}

example10() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    ITV1 19790717T222905 0 0 0 x RI/T        ITV1 SCAN SCAN

    ITV1 19790717T222935 0 0 0 1 0717T2228   ITV1 PTR  SCAN

    ITV1 19790717T223005 0 0 0 0 0717T2228   ITV1 REC  SCAN

    # ITV1 19790717T235920 0 0 1 1 0718T0000   ITV1 SCAN SCAN
    ITV1 19790717T235920 0 0 1 1 0718T0000   ITV1 REC PTR
    # The VCR 1 SCAN state at 235920 seems to be wrong because EN 300
    # 231 Section 6.2 p) suggests that recording stops with a 30
    # second delay after the 0717T2228 label with MI=0 is no longer
    # present. The VCR 2 SCAN state seems to be wrong because the
    # label 0000 of the first program is already present with PRF=1.

    ITV1 19790717T235935 0 0 1 1 0718T0000   ITV1
    # ITV1 19790717T235935 1 0 1 1 0717T2229/Q ITV1 PTR  PTR
    ITV1 19790717T235935 1 0 1 1 0717T2229/Q ITV1 REC  PTR
    # See previous comment.

    ITV1 19790717T235950 0 0 1 0 0718T0000   ITV1
    ITV1 19790717T235950 1 0 1 1 0717T2229/Q ITV1 PTR  REC

    ITV1 19790718T000005 0
    ITV1 19790718T000005 1 0 1 0 0717T2229/Q ITV1 REC  REC
    # The VCR 2 REC state at 000005 seems to be wrong because the
    # label 0718T0000 with MI=1 disappeared and a new valid label
    # 0717T2229 is present, requiring an immediate stop. According to
    # Table E.19 and E.20 VCR 2 is supposed to stop 30 seconds after
    # recording started at 19790717T235950, but the labels in Table
    # E.20 don't seem to support this. The VCR 2 state should be STBY
    # because the 0718T0000 program finally ended and should be
    # removed from the schedule.

    ITV1 19790718T000006 1 0 1 0 0717T2229/Q ITV1 REC  STBY
    # However as in Example 2 pdc2 needs one cycle to acknowledge the
    # absence of 0718T0000 on channel 0. This line is not in Table
    # E.20.

    ITV1 19790718T000020 1 0 1 0 0717T2229/Q ITV1 REC  STBY
  `

  echo "EN 300 231 Annex E.3 Example 10, VCR 1:"
  echo "$table" | sed "$re12" | ./pdc2 $opts \
    1979-07-17 22:28 22:28 22:28 \
    1979-07-17 22:29 22:29 22:29

  echo "EN 300 231 Annex E.3 Example 10, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-18 00:00 00:00 00:00
}

example11() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    CHL4 19790702T160006 0 0 1 x RI/T      CHL4 SCAN SCAN
    # Note recording stops here after it started as scheduled at 1600
    # when no VPS/PDC signal was received.

    CHL4 19790702T160015 0 0 1 0 0702T1600 CHL4 REC  SCAN

    # CHL4 19790702T160045 0 1 1 0 0705T0123 CHL4 REC  SCAN
    CHL4 19790702T160045 0 1 1 0 0705T0123 CHL4 STBY SCAN
    # The VCR 1 REC state in EN 300 231 Annex E.3 Table E.22 at
    # 19790702T160045ff seems to be wrong because the label 0705T0123
    # with LUF=1 immediately terminates the label 0702T1600 with
    # MI=1. Table E.21 agrees with this interpretation "for VCRs
    # responsive to MI". Since the 1600 program has been rescheduled
    # and all scheduled programs now start on 19790705 the VCR 1 state
    # should be STBY until further.

    # CHL4 19790702T160057 0 0 1 1 0702T1559 CHL4 SCAN PTR
    CHL4 19790702T160057 0 0 1 1 0702T1559 CHL4 STBY PTR
    # See previous comment.

    # CHL4 19790702T160127 0 0 1 0 0702T1559 CHL4 SCAN REC
    CHL4 19790702T160127 0 0 1 0 0702T1559 CHL4 STBY REC

    # CHL4 19790702T162010 0                      SCAN REC
    CHL4 19790702T162010 0                      STBY REC
    # VPS/PDC signal lost at this point. Note as in Example 2 pdc2
    # needs another cycle to acknowledge the absence of the 0702T1559
    # label but it keeps recording for 30 seconds as shown in Example
    # 11.

    # CHL4 19790702T162040 0                      SCAN SCAN
    CHL4 19790702T162040 0                      STBY SCAN

    CHL4 19790703T040000 0                      STBY STBY
    # Example 11 does not explain what happens at 19790702T162040 in
    # VCR 2. Supposedly since the duration of the first program is
    # unknown, recording stops 30 seconds after the VPS/PDC signal was
    # lost at 162010, but the program remains scheduled (SCAN state at
    # 162040) until its PIL validity window ends at
    # 19790703T040000. This line is not in Table E.22.

    CHL4 19790704T200000 0                      SCAN STBY
    # Table E.22 does not show the VCR 1 state change from STBY to
    # SCAN at 19790704T200000 as we enter the PIL validity window of
    # the program rescheduled to 19790705T0123.

    CHL4 19790705T000000 0                      SCAN SCAN
    # Table E.22 does not show the VCR 2 state change from STBY to
    # SCAN at 19790705T000000 as we enter the PIL validity window of
    # the program scheduled for 19790705T1000.

    CHL4 19790705T012300 0                      REC  SCAN
    # VCR 1 records the rescheduled program 19790705T0123 by timer
    # for the originally scheduled duration.

    CHL4 19790705T015800 0                      SCAN SCAN

    CHL4 19790705T095950 0 0 1 0 0705T0123 CHL4 SCAN SCAN
    # As explained in the comments to Example 11 VCR 1 ignores this
    # label because the program was removed from the schedule at
    # 19790705T0158.

    CHL4 19790705T100020 0 1 1 0 0705T1459 CHL4
    CHL4 19790705T100020 1 0 1 0 0705T1000 CHL4 SCAN REC

    CHL4 19790705T100050 0
    CHL4 19790705T100050 1 0 1 0 0705T1000 CHL4 SCAN REC

    CHL4 19790705T145940 0 0 0 0 0705T1430 CHL4
    CHL4 19790705T145940 1 0 1 1 0705T1500 CHL4 REC  STBY

    CHL4 19790705T145945 0
    CHL4 19790705T145945 1 0 1 1 0705T1500 CHL4 REC  STBY

    CHL4 19790705T145955 1 0 1 1 0705T1500 CHL4
    CHL4 19790705T145955 2 0 1 1 0705T1459 CHL4
    CHL4 19790705T145955 3 0 1 1 0705T0123 CHL4 REC  STBY

    CHL4 19790705T150010 1 0 1 0 0705T1500 CHL4
    CHL4 19790705T150010 2 0 1 1 0705T1459 CHL4
    CHL4 19790705T150010 3 0 1 1 0705T0123 CHL4 REC  STBY

    CHL4 19790705T150015 1 0 1 0 0705T1500 CHL4
    CHL4 19790705T150015 2 0 1 1 0705T1459 CHL4
    CHL4 19790705T150015 3 0 1 1 0705T0123 CHL4 STBY STBY

    CHL4 19790705T150025 1
    CHL4 19790705T150025 2 0 1 0 0705T1459 CHL4
    CHL4 19790705T150025 3 0 1 0 0705T0123 CHL4 STBY STBY
  `

  # Test omitted because ./pdc2 does not support VCR reprogramming.
#  echo "EN 300 231 Annex E.3 Example 11, VCR 1:"
#  echo "$table" | sed "$re12" | ./pdc2 $opts \
#    1979-07-02 16:00 16:35 16:00 \
#    1979-07-05 14:30 14:30 14:30

  echo "EN 300 231 Annex E.3 Example 11, VCR 2:"
  echo "$table" | sed "$re22" | ./pdc2 $opts \
    1979-07-02 15:59 15:59 15:59 \
    1979-07-05 10:00 10:00 10:00
}

example12() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    BBC1 19791201T133000 0 0 1 0 1201T1330/S BBC1

    BBC1 19791201T135900 0 0 1 0 1201T1330/S BBC1 REC  SCAN SCAN

    BBC1 19791201T135940 0
    BBC1 19791201T135940 1 0 1 1 NSPV/S      BBC1 REC  SCAN SCAN
    # The VCR 1 REC state at 135940 seems to be wrong because the
    # label 1330 with MI=1 is no longer present.

    BBC1 19791201T135941 1 0 1 1 NSPV/S      BBC1 SCAN SCAN SCAN
    # However as in Example 2 pdc2 will not acknowledge the absence
    # of the label on channel 0 until one cycle later. This line
    # is not in Table E.24.

    BBC1 19791201T140010 1 0 1 0 NSPV/S      BBC1 SCAN SCAN SCAN

    BBC1 19791201T140205 1 0 1 1 1201T1400/S BBC1 SCAN SCAN PTR

    BBC1 19791201T140235 1 0 1 0 1201T1400/S BBC1 SCAN SCAN REC

    BBC1 19791201T144540 0 0 1 1 1201T1440/S BBC1
    BBC1 19791201T144540 1 0 1 0 1201T1400/S BBC1 PTR  SCAN REC

    BBC1 19791201T144610 0 0 1 0 1201T1440/S BBC1
    BBC1 19791201T144610 1 0 1 x INT         BBC1 REC  SCAN SCAN

    BBC1 19791201T144615 0 0 1 0 1201T1440/S BBC1
    BBC1 19791201T144615 1                        REC  SCAN SCAN

    BBC1 19791201T145840 0 0 1 0 1201T1440/S BBC1
    BBC1 19791201T145840 2 0 1 1 1201T1510/S BBC1 REC  PTR  SCAN

    BBC1 19791201T145850 0 0 1 x INT         BBC1
    BBC1 19791201T145850 2 0 1 0 1201T1510/S BBC1 SCAN REC  SCAN

    BBC1 19791201T145955 0
    BBC1 19791201T145955 2 0 1 0 1201T1510/S BBC1 SCAN REC  SCAN

    BBC1 19791201T155000 1 0 1 0 1201T1400/S BBC1
    BBC1 19791201T155000 2                        SCAN REC  REC
    # The VCR 2 REC state at 155000 seems to be wrong because the
    # label 1510 with MI=1 disappeared and a new valid label 1400
    # is present.

    BBC1 19791201T155001 1 0 1 0 1201T1400/S BBC1 SCAN SCAN REC
    # However as in Example 2 pdc2 will not acknowledge the absence
    # of the label on channel 2 until one cycle later. This line
    # is not in Table E.24.

    BBC1 19791201T155030 1 0 1 0 1201T1400/S BBC1 SCAN SCAN REC

    BBC1 19791201T155050 0 0 1 1 1201T1440/S BBC1
    BBC1 19791201T155050 1 0 1 0 1201T1400/S BBC1
    BBC1 19791201T155050 2 0 1 1 1201T1510/S BBC1 PTR  SCAN REC

    BBC1 19791201T155150 0 0 1 1 1201T1440/S BBC1
    BBC1 19791201T155150 1 0 1 0 1201T1400/S BBC1
    BBC1 19791201T155150 2 0 1 1 1201T1510/S BBC1 REC  SCAN REC
    # The VCR 1 REC state at 155150 is correct. As in Example 13
    # recording commences after PRF on LC 0 was not cleared within one
    # minute so we do not grind a hole into the media.

    BBC1 19791201T160410 0
    BBC1 19791201T160410 1 0 1 0 1201T1400/S BBC1
    BBC1 19791201T160410 2
    BBC1 19791201T160410 3 0 1 1 1201T1600/S BBC1 REC  PTR  REC
    # The VCR 1 REC state at 160410 seems to be wrong because the
    # label 1440 with MI=1 is gone and a new label 1400 is present.
    # The new state should be STBY after the program was removed
    # from the schedule.

    BBC1 19791201T160411 1 0 1 0 1201T1400/S BBC1
    BBC1 19791201T160411 3 0 1 1 1201T1600/S BBC1 STBY PTR  REC
    # However as in Example 2 pdc2 will not acknowledge the absence
    # of the label on channel 0 and 2 until one cycle later. These
    # lines are not in Table E.24.

    BBC1 19791201T160440 1
    BBC1 19791201T160440 3 0 0 0 1201T1600/S BBC1 STBY REC  REC
    # The VCR 3 REC state at 160440 seems to be wrong because the
    # label 1400 with MI=1 is gone and a new label 1600 is present.

    BBC1 19791201T160441 3 0 0 0 1201T1600/S BBC1 STBY REC  STBY
    # However as in Example 2 pdc2 will not acknowledge the absence of
    # the label on channel 1 until one cycle later. This line is not
    # in Table E.24.

    BBC1 19791201T160510 3 0 0 0 1201T1600/S BBC1 STBY REC  STBY
  `

  echo "EN 300 231 Annex E.3 Example 12, VCR 1:"
  echo "$table" | sed "$re13" | ./pdc2 $opts \
    1979-12-01 13:30 13:30 13:30 \
    1979-12-01 14:40 14:40 14:40

  echo "EN 300 231 Annex E.3 Example 12, VCR 2:"
  echo "$table" | sed "$re23" | ./pdc2 $opts \
    1979-12-01 15:10 15:10 15:10 \
    1979-12-01 16:00 16:00 16:00

  echo "EN 300 231 Annex E.3 Example 12, VCR 3:"
  echo "$table" | sed "$re33" | ./pdc2 $opts \
    1979-12-01 14:00 14:00 14:00
}

example13a() {
  table=`cat <<EOF
    #                LCI LUF MI PRF PIL/PTY CNI
    BBC1 19791201T135900 0 0 1 0 1201T1330 BBC1 REC  SCAN SCAN

    BBC1 19791201T135940 0 0 1 0 1201T1330 BBC1
    BBC1 19791201T135940 1 0 1 1 1201T1400 BBC1 REC  SCAN PTR

    BBC1 19791201T140010 0
    # BBC1 19791201T140010 1 0 1 0 1201T1400 BBC1 REC  SCAN REC
    BBC1 19791201T140010 1 0 1 0 1201T1400 BBC1 SCAN SCAN REC
    # The VCR 1 REC state at 140010 seems to be wrong because the
    # label 1330 with MI=1 is gone and a new valid label 1400 is
    # present. Table E.25: "VCRs which were recording against label
    # 1330 and which do not respond to MI continue to record for 30
    # s."

    BBC1 19791201T140040 1 0 1 0 1201T1400 BBC1 SCAN SCAN REC

    BBC1 19791201T140205 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T140205 2 0 1 1 NSPV/G    BBC1 PTR  SCAN REC

    BBC1 19791201T140235 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T140235 2 0 1 0 NSPV/G    BBC1 REC  SCAN REC

    BBC1 19791201T144540 0 0 1 1 NSPV/M    BBC1
    BBC1 19791201T144540 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T144540 2 0 1 0 NSPV/G    BBC1 REC  SCAN REC

    BBC1 19791201T144610 0 0 1 0 NSPV/M    BBC1
    BBC1 19791201T144610 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T144610 2 0 1 x INT/G     BBC1 SCAN SCAN REC

    BBC1 19791201T145840 0 0 1 0 NSPV/M    BBC1
    BBC1 19791201T145840 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T145840 2
    BBC1 19791201T145840 3 0 1 1 NSPV/R    BBC1 SCAN PTR  REC

    BBC1 19791201T145940 0 0 1 1 NSPV/M    BBC1
    BBC1 19791201T145940 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T145940 2 0 1 0 NSPV/G    BBC1
    BBC1 19791201T145940 3 0 1 1 NSPV/R    BBC1 REC  REC  REC
    # The VCR 2 REC state at 145940 is correct. As explained in the
    # comments to Example 13 recording commences after PRF was not
    # cleared within one minute.

    BBC1 19791201T155050 0 0 1 1 NSPV/M    BBC1
    BBC1 19791201T155050 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T155050 2 0 1 0 NSPV/G    BBC1
    BBC1 19791201T155050 3 0 1 1 NSPV/R    BBC1 REC  REC  REC
    # The VCR 2 REC state at 155050 is correct. VCR 2 overrides PRF
    # since 145940.

    BBC1 19791201T160410 0 0 1 1 1201T1600 BBC1
    BBC1 19791201T160410 1 0 1 0 1201T1400 BBC1
    BBC1 19791201T160410 2 0 1 0 NSPV/G    BBC1
    # BBC1 19791201T160410 3                      REC  REC  REC
    BBC1 19791201T160410 3                      REC  SCAN REC
    # The VCR 2 REC state at 160410 seems to be wrong because the
    # label NSPV/R with MI=1 is gone and new valid labels 1400 and
    # NSPV/G are present. It makes sense however if VCR 2 is not
    # responsive to MI.

    BBC1 19791201T160440 0 0 1 0 1201T1600 BBC1
    BBC1 19791201T160440 1
    # BBC1 19791201T160440 2                      REC  SCAN STBY
    BBC1 19791201T160440 2                      SCAN SCAN REC
    # The VCR 1 REC state at 160440 seems to be wrong because the
    # label NSPV/G with MI=1 is gone and a new label 1600 is present.
    # Table E.25 states: "Any VCRs which had been recording from the
    # Sports magazine programme, and which are not pre-programmed with
    # the 1600 label will stop (after 30 s, if not responsive to MI),
    # and will resume scanning." Also the VCR 3 REC state seems to be
    # wrong because the label 1400 is gone and a new label 1600 is
    # present.

    BBC1 19791201T160441 0 0 1 0 1201T1600 BBC1 SCAN SCAN STBY
    # However as in Example 2 pdc2 needs one cycle to acknowledge the
    # absence of 1201T1400 on channel 1. This line is not in Table
    # E.26.

    BBC1 19791201T160510 0 0 1 0 1201T1600 BBC1 SCAN SCAN STBY
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 13, VCR 1:"
#  echo "$table" | sed "$re13" | ./pdc2 $opts \
#    1979-12-01 13:30 13:30 13:30 \
#    1979-12-01 00:00 00:30 G

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 13, VCR 2:"
#  echo "$table" | sed "$re23" | ./pdc2 $opts \
#    1979-12-01 00:00 00:00 R

  echo "EN 300 231 Annex E.3 Example 13, VCR 3:"
  echo "$table" | sed "$re33" | ./pdc2 $opts \
    1979-12-01 14:00 14:00 14:00
}

example13b() {
  # The VCR states in Example 13 seem to describe VCRs not responsive
  # to MI, so let's try Example 13 with MI = 0 throughout.

  table=`cat <<EOF
    #              LCI, LUF, MI, PRF, PIL, CNI, PTY
    BBC1 19791201T135900 0 0 0 0 1201T1330 BBC1 REC  SCAN SCAN

    BBC1 19791201T135940 0 0 0 0 1201T1330 BBC1
    BBC1 19791201T135940 1 0 0 1 1201T1400 BBC1 REC  SCAN PTR

    BBC1 19791201T140010 0
    BBC1 19791201T140010 1 0 0 0 1201T1400 BBC1 REC  SCAN REC

    BBC1 19791201T140040 1 0 0 0 1201T1400 BBC1 SCAN SCAN REC

    BBC1 19791201T140205 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T140205 2 0 0 1 NSPV/G    BBC1 PTR  SCAN REC

    BBC1 19791201T140235 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T140235 2 0 0 0 NSPV/G    BBC1 REC  SCAN REC

    BBC1 19791201T144540 0 0 0 1 NSPV/M    BBC1
    BBC1 19791201T144540 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T144540 2 0 0 0 NSPV/G    BBC1 REC  SCAN REC

    BBC1 19791201T144610 0 0 0 0 NSPV/M    BBC1
    BBC1 19791201T144610 1 0 0 0 1201T1400 BBC1
    # BBC1 19791201T144610 2 0 0 x INT/G     BBC1 SCAN SCAN REC
    BBC1 19791201T144610 2 0 1 x INT       BBC1 SCAN SCAN REC
    # The VCR 1 REC state at 144610 is only correct if INT is
    # transmitted with MI=1 and takes effect immediately as shown in
    # Table E.26. The series code G may contradict Table E.7: "It is
    # not permitted to send a PTY, set to 3F to denote an emergency
    # message, with INT." However that INT code terminates a valid
    # label, not a NSPV/3F code, which is in fact transmitted in
    # parallel. All other examples show INT and RI/T codes with PTY
    # 0x00 only, in particular Example 12 where INT/00 terminates
    # 1201T1400/S. Considering that service codes apply to the valid
    # label formerly transmitted on the same label channel (here
    # NSPV/G) the PTY isn't necessary either.

    BBC1 19791201T145840 0 0 0 0 NSPV/M    BBC1
    BBC1 19791201T145840 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T145840 2
    BBC1 19791201T145840 3 0 0 1 NSPV/R    BBC1 SCAN PTR  REC

    BBC1 19791201T145940 0 0 0 1 NSPV/M    BBC1
    BBC1 19791201T145940 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T145940 2 0 0 0 NSPV/G    BBC1
    BBC1 19791201T145940 3 0 0 1 NSPV/R    BBC1 REC  REC  REC
    # The VCR 2 REC state at 145940 is correct. As explained in the
    # comments to Example 13 recording commences after PRF was not
    # cleared within one minute.

    BBC1 19791201T155050 0 0 0 1 NSPV/M    BBC1
    BBC1 19791201T155050 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T155050 2 0 0 0 NSPV/G    BBC1
    BBC1 19791201T155050 3 0 0 1 NSPV/R    BBC1 REC  REC  REC
    # The VCR 2 REC state at 155050 is correct. VCR 2 overrides PRF
    # since 145940.

    BBC1 19791201T160410 0 0 0 1 1201T1600 BBC1
    BBC1 19791201T160410 1 0 0 0 1201T1400 BBC1
    BBC1 19791201T160410 2 0 0 0 NSPV/G    BBC1
    BBC1 19791201T160410 3                      REC  REC  REC

    BBC1 19791201T160440 0 0 0 0 1201T1600 BBC1
    BBC1 19791201T160440 1
    # BBC1 19791201T160440 2                      REC  SCAN STBY
    BBC1 19791201T160440 2                      REC  SCAN REC
    # The VCR 3 STBY state at 160440 is not correct if label 1400 is
    # transmitted with MI=0 at 160410 because MI=0 delays the stop by
    # 30 seconds. Note as in Example 2 pdc2 needs another cycle to
    # acknowledge the absence of the 1201T1401 label.

    BBC1 19791201T160510 0 0 0 0 1201T1600 BBC1 SCAN SCAN STBY
  `

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 13, VCR 1 not resp. to MI:"
#  echo "$table" | sed "$re13" | ./pdc2 $opts \
#    1979-12-01 13:30 13:30 13:30 \
#    1979-12-01 00:00 00:30 G

  # Test omitted because ./pdc2 does not support recording by PTY.
#  echo "EN 300 231 Annex E.3 Example 13, VCR 2 not resp. to MI:"
#  echo "$table" | sed "$re23" | ./pdc2 $opts \
#    1979-12-01 00:00 00:00 R

  echo "EN 300 231 Annex E.3 Example 13, VCR 3 not resp. to MI:"
  echo "$table" | sed "$re33" | ./pdc2 $opts \
    1979-12-01 14:00 14:00 14:00
}

example14() {
  table=`cat <<EOF
    #                 LCI LUF MI PRF PIL/PTY CNI
    ITV1 19791028T010116Z 0 0 0 0 1028T0159 ITV1 REC  SCAN
    # EN 300 231 Annex E.3 Table E.28 specifies the event time as
    # local time, here 02:01:16 in the BST zone (UTC + 1 hour).
    # We give the time in the UTC zone to avoid ambiguity.

    ITV1 19791028T015830Z 0 0 0 0 RI/T      ITV1 REC  SCAN
    ITV1 19791028T015900Z 0 0 0 0 RI/T      ITV1 STBY SCAN
    ITV1 19791028T015915Z 0 0 0 0 RI/T      ITV1 STBY SCAN

    ITV1 19791028T015945Z 0 0 1 0 1028T0200 ITV1 STBY REC
    # Daylight saving time ends at 1979-10-28 02:00:00 UTC.

    ITV1 19791028T020015Z 0 0 1 0 1028T0200 ITV1 STBY REC
    # This label is transmitted at 02:00:15 local time in the GMT
    # zone (UTC + 0 hours).
  `

  echo "EN 300 231 Annex E.3 Example 14, VCR 1:"
  echo "$table" | sed "$re12" | TZ="Europe/London" ./pdc2 $opts \
    1979-10-28 01:59 01:59 01:59

  echo "EN 300 231 Annex E.3 Example 14, VCR 2:"
  echo "$table" | sed "$re22" | TZ="Europe/London" ./pdc2 $opts \
    1979-10-28 02:00 02:00 02:00
}

(
    example1
    example2
    example3
    example4a
    example4b
    example5
    example6
    example7
    example8
    example9
    example10
    example11
    example12
    example13a
    example13b
    example14
) >/dev/null 2>&1 # quiet for make check
