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
      hardtimelimit          "Hard timelimit that is applied to the time since the job has been submitted. If the job does not finish within this limit, it will be canceled by the GAMS/SolveEngine link."
      printjoblist           Prints list of SolveEngine jobs
      debug                  Enabling debug output
      deletejob              Whether to delete job at termination
      verifycert             Whether to verify SSL certificate using the machines CA certificates storage
* immediates
      nobounds               ignores bounds on options
      readfile               read secondary option file
    /

$onembedded
optdata(g,o,t,f) /
general.(
  apikey          .s.(def '')
  hardtimelimit   .r.(def maxdouble)
  printjoblist    .b.(def 0)
  debug           .i.(def 0, up 2)
  deletejob       .b.(def 1)
  verifycert      .b.(def 1)
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
 hidden(o)         / NoBounds, ReadFile, deletejob /
$onempty
 odefault(o)       / hardtimelimit '&infin;' /
$offempty
$onempty
 oep(o) enum options for documentation only / /;
$offempty
