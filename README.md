### Clockipoodle
Clockipoodle is a clock module designed for use in a Eurorack modular synthesizer.

It's based on an Arduino nano, driving a Darlington Array, the UI is:
- a 128x64 pixel SSD1306 display,
- a switched rotary encoder,
- 2 x momentary buttons
- 6 x 3.5mm standard jacks

As of version 1.1 it supports the following options:
1. Direct setting of BPM via a rotary encoder.
2. Indirect setting of the BPM, using the rotary encoder to set, then pressing the encoder to apply.
3. Four additional outputs labelled A through to D that can be a multuplied, divided, duplicate or a disabled copy of the main clock.
4. Tap to set BPM.
5. Adjustable pulse width.
6. Play/Pause.
7. Reset the various outputs to be in sync again.

There is also an unused input currenly labeled as 'CV IN' and and associated LED, which may become a Clock in or CV input that drives a parameter (or parameters) to be later defined.
The current design is limited to 0-5V so could only really increase the tempo, so the next iteration is likely to support -5 to +5v so that decreasing tempo from a CV input is an option.
In the meantime I'm considering using the CV input to adjust a swing component of the beat.

All input and output jacks are protected against over voltage and negative voltages for safety.

This is my first stab at a module design, comments are welcome.
