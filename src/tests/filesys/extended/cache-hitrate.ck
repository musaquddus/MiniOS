# -*- perl -*-
use strict;
use warnings;
use tests::tests;
use tests::random;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hitrate) begin
(cache-hitrate) create "benZ"
(cache-hitrate) open "benZ"
(cache-hitrate) write random bytes to "benZ"
(cache-hitrate) reset cache, hitrate is 0 percent
(cache-hitrate) reading from "benZ"
(cache-hitrate) reading from "benZ"
(cache-hitrate) cache hitrate improved!
(cache-hitrate) end
EOF
pass;