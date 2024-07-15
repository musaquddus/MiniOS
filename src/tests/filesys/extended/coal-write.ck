# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(coal-write) begin
(coal-write) create "benZ2"
(coal-write) open "benZ2"
(coal-write) wrote 64 kiB to "benZ2"
(coal-write) block writes less than 129!
(coal-write) end
EOF
pass;