# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(custom-1) begin
(child-simple) run
child-simple: exit(81)
(custom-1) end
custom-1: exit(0)
EOF
pass;
