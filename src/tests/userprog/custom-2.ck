# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(custom-2) begin
(custom-2) end
custom-2: exit(0)
EOF
pass;
