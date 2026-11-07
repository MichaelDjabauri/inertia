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

# The dock's buttons, left to right, all at y = 600.
inspect=241; grab=284; throw=327; pause=381; step=424; restart=467; slow=518; vectors=561; settings=604
tool() { print "5:press:$1,600;7:release" }
block='358,364'   # the bottom block in Centre of Mass (scene 2)
stage='420,300'   # open space in the middle of the stage
cradle='90,259'   # Newton's Cradle in the lesson list

# Projectile Motion (scene 0) launches its own ball at 0.6 s, so it starts at bodies=1.
check "throw tool drops a ball"         'bodies=2'                 140 0 "$(tool $throw);100:press:$stage;102:release"
check "dragging throws a ball"          'bodies=2'                 200 0 "$(tool $throw);100:press:$stage;110:move:520,200;130:release"
check "clicking the dock makes no body" 'bodies=1 .* slow=1'       140 0 "$(tool $throw);100:press:$slow,600;102:release"
check "hidden UI lets clicks through"   'bodies=2'                 140 0 "$(tool $throw);10:key:H;100:press:$cradle;102:release"
check "inspect selects a body"          'selected=1'                80 2 "60:press:$block;62:release"
check "right-click removes a body"      'bodies=5'                  80 2 "60:rpress:$block;62:rrelease"
check "grab holds a body"               'joints=1'                 130 2 "$(tool $grab);100:press:$block;120:move:420,200"
check "letting go frees it"             'joints=0'                 140 2 "$(tool $grab);100:press:$block;120:move:420,200;130:release"
check "removing a held body is safe"    'bodies=5'                 140 2 "$(tool $grab);100:press:$block;105:move:420,250;125:rpress:420,250;127:release;128:rrelease"
check "changing lesson while holding"   'scene=Hanging Chain'      150 2 "$(tool $grab);100:press:$block;105:move:420,250;110:key:4"
check "pause button"                    'paused=1'                  60 0 "30:press:$pause,600;32:release"
check "step button"                     'paused=1'                  60 0 "30:press:$step,600;32:release"
check "restart button"                  'bodies=1'                 200 0 "$(tool $throw);60:press:$stage;62:release;100:press:$restart,600;102:release"
check "vectors button"                  'vectors=0'                 60 0 "30:press:$vectors,600;32:release"
check "settings button"                 'settings=1'                40 0 "20:press:$settings,600;22:release"
check "gravity slider"                  'gravity=0.00'              60 0 "20:press:$settings,600;22:release;30:press:378,214;35:release"
check "lesson list"                     "scene=Newton's Cradle"     80 0 "20:press:$cradle;22:release"
# The Pendulum lesson (scene 6): its tabs, then its length slider, dragged to the far end.
check "experiment tab"                  'tab=1'                     40 6 "10:press:896,94;12:release"
check "learn tab"                       'tab=0'                     60 6 "10:press:896,94;12:release;30:press:753,94;32:release"
check "a setting restarts the lesson"   'tab=1 .*params=12.0,2.0,30' 90 6 "10:press:896,94;12:release;30:press:968,253;35:release"
check "wheel zooms"                     'zoom=1\.57'                80 0 "50:move:$stage;51:wheel:5"
check "space pauses"                    'paused=1'                  30 0 "10:key:SPACE"
check "H hides the UI"                  'ui=0'                      30 0 "10:key:H"
check "K colours by speed"              'heat=1'                    30 0 "10:key:K"
check "V hides the vectors"             'vectors=0'                 30 0 "10:key:V"
check "S slows time"                    'slow=1'                    30 0 "10:key:S"
check "number keys change lesson"       'scene=Friction on a Slope' 60 0 "10:key:2"
check "R restarts"                      'bodies=1'                 120 0 "$(tool $throw);20:press:$stage;22:release;40:key:R"
check "resolution can change mid-run"   'bodies=1'                 200 0 "60:scale:0.6;120:scale:0.75"
check "every lesson loads in turn"      'scene=Chaos'              400 0 "30:key:];60:key:];90:key:];120:key:];150:key:];180:key:];210:key:];240:key:];270:key:];300:key:];330:key:];360:key:]"

print "\n$failures failed"
exit $((failures > 0))
