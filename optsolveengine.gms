$eolcom //
$setglobal SHORTDOCONLY

set g SolveEngine Option Groups /
        general        General Options
      /
    e / 1*100 /
    f / def Default, lo Lower Bound, up Upper Bound, ref Reference /
    t / I   Integer
        R   Real
        S   String
        B   Binary
      /
    o Options /
      apikey                 Satalia SolveEngine API key
      printjoblist           Prints list of SolveEngine jobs
      debug                  Enabling debug output
      deletejob              Whether to delete job at termination
* immediates
      nobounds               ignores bounds on options
      readfile               read secondary option file
    /

$onembedded
optdata(g,o,t,f) /
general.(
  apikey          .s.(def '')
  printjoblist    .b.(def 0)
  debug           .i.(def 0, up 2)
  deletejob       .b.(def 1)
* immediates
  nobounds        .b.(def 0)
  readfile        .s.(def '')
) /
$onempty
  oe(o,e) /
  /
$offempty
 im  immediates recognized  / EolFlag , ReadFile, Message, NoBounds /
 immediate(o,im)   / NoBounds.NoBounds, ReadFile.ReadFile /
 hidden(o)         / NoBounds, ReadFile /
$onempty
 odefault(o)       / /
$offempty
$onempty
 oep(o) enum options for documentation only / /;
$offempty
