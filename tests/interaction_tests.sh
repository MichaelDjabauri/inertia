#!/bin/zsh
# Drives the real app with scripted input and checks the state it reports on exit.
# Each case opens a window briefly, so this needs a display of at least 1000x640.
# Layout coordinates below are UI points for that window size.
#
#   tests/interaction_tests.sh [path/to/inertia]

app=${1:-${0:A:h}/../build/inertia}
failures=0

check() {
    local name=$1 wanted=$2 frames=$3 scene=$4 script=$5
    local out=$(perl -e 'alarm 60; exec @ARGV' "$app" --size 1000x640 --render-scale 1 --report \
                --scene $scene --frames $frames --script "$script" 2>&1 | tail -1)
    if [[ $out =~ $wanted ]]; then
        print "ok    $name"
    else
        print "FAIL  $name\n      wanted $wanted\n      got    $out"
        failures=$((failures + 1))
    fi
}

grab='5:press:385,596;7:release'    # selects the Grab tool in the dock
box='625,440'                       # the middle of a box in the Pyramid scene

check "click drops a ball"              'bodies=91'             140 0 "100:press:500,200;102:release"
check "drag throws a ball"              'bodies=91'             200 0 "100:press:500,200;110:move:620,130;130:release"
check "clicking the dock makes no body" 'bodies=90'             200 0 "100:press:655,596;102:release"
check "hidden UI lets clicks through"   'bodies=91'             140 0 "10:key:H;100:press:90,222;102:release"
check "right-click removes a body"      'bodies=135'            110 1 "100:rpress:$box;102:rrelease"
check "grab holds a body"               'joints=1'              130 1 "$grab;100:press:$box;120:move:595,200"
check "letting go frees it"             'joints=0'              140 1 "$grab;100:press:$box;120:move:595,200;130:release"
check "removing a held body is safe"    'bodies=135'            130 1 "$grab;100:press:$box;105:move:595,300;110:rpress:595,300;115:release;116:rrelease"
check "changing scene while holding"    'scene=Dominoes'        150 1 "$grab;100:press:$box;105:move:595,300;110:key:3"
check "pause button"                    'paused=1'              130 0 "100:press:665,596;102:release"
check "step button"                     'paused=1'              130 0 "100:press:710,596;102:release"
check "restart button"                  'bodies=90'             200 0 "60:press:500,200;62:release;100:press:755,596;102:release"
check "settings button"                 'settings=1'             40 0 "20:press:808,596;22:release"
check "gravity slider"                  'gravity=0.00'           60 0 "20:press:808,596;22:release;30:press:810,184;35:release"
check "scene list"                      "scene=Newton's Cradle"  80 0 "20:press:90,222;22:release"
check "wheel zooms"                     'ppm=3[0-9]'             80 0 "50:move:600,300;51:wheel:5"
check "space pauses"                    'paused=1'               30 0 "10:key:SPACE"
check "H hides the UI"                  'ui=0'                   30 0 "10:key:H"
check "V colours by speed"              'heat=1'                 30 0 "10:key:V"
check "number keys change scene"        'scene=Pyramid'          60 0 "10:key:2"
check "R restarts"                      'bodies=90'              90 0 "10:key:R"
check "blast tool"                      'tool=4'                160 0 "20:press:564,596;22:release;100:press:600,400;102:release"
check "pull tool"                       'tool=5'                160 0 "20:press:609,596;22:release;100:press:600,300;150:release"
check "resolution can change mid-run"   'bodies=90'             200 0 "60:scale:0.6;120:scale:0.75"
check "every scene loads in turn"       'scene=Rover'           400 0 "30:key:];60:key:];90:key:];120:key:];150:key:];180:key:];210:key:];240:key:];270:key:];300:key:];330:key:]"

print "\n$failures failed"
exit $((failures > 0))
