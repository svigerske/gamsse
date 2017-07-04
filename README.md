# gamsse

This is a very basic code to call the Satalia SolveEngine from GAMS.
It translate an LP/MIP instance from GAMS to an .lp file, submits this to SolveEngine,
waits for SolveEngine to solve it, and prints the result to the screen.

To build, create a symlink "gams" pointing to a GAMS system directory.
Then call make. We assume Linux, maybe MacOS X will work too.

Run GAMS on a linear model with option keep=1 (and any GAMS solver).
Then call the gamsse executable with the path to the GAMS control file file (e.g., 225a/gamscntr.dat).
Optionally, pass the name of a GAMS/SolveEngine options file as additional argument.
