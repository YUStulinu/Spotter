// id, name, signal, direction, amplitude, count at the far end, shortest repetition (s).
// Written by train/spotter_train/tune_reps.py from MM-Fit's training workouts; the deadlift,
// which MM-Fit does not have, is set by hand.
{"squats", "Squat", Signal::HipHeight, -1, 0.4254f, false, 0.6},
{"lunges", "Lunge", Signal::HipHeight, -1, 0.3687f, false, 0.6},
{"pushups", "Push-up", Signal::HipHeight, -1, 0.2315f, false, 0.6},
{"situps", "Sit-up", Signal::NoseHeight, -1, 0.3228f, false, 0.6},
{"jumping_jacks", "Jumping jack", Signal::WristLift, 1, 0.7895f, false, 0.4},
{"bicep_curls", "Biceps curl", Signal::ElbowAngle, -1, 46.47f, false, 0.6},
{"tricep_extensions", "Triceps extension", Signal::WristLift, 1, 0.3511f, false, 0.6},
{"dumbbell_rows", "Dumbbell row", Signal::ElbowAngle, -1, 45.56f, false, 0.6},
{"dumbbell_shoulder_press", "Shoulder press", Signal::ElbowAngle, 1, 53.6f, false, 0.6},
{"lateral_shoulder_raises", "Lateral raise", Signal::WristLift, 1, 0.9606f, false, 0.6},
{"deadlift", "Deadlift", Signal::HipAngle, 1, 60.f, true, 1.0},
