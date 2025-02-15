#!/usr/bin/env perl
if (!$::Driver) { use FindBin; exec("$FindBin::Bin/bootstrap.pl", @ARGV, $0); die; }
# DESCRIPTION: Verilator: Verilog Test driver/expect definition
#
# Copyright 2022 by Antmicro Ltd. This program is free software; you
# can redistribute it and/or modify it under the terms of either the GNU
# Lesser General Public License Version 3 or the Perl Artistic License
# Version 2.0.
# SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

scenarios(simulator => 1);

if (!$Self->have_coroutines) {
    skip("No coroutine support");
}
else {
    top_filename("t/t_timing_clkgen1.v");

    compile(
        verilator_flags2 => ["--timing --trace -Wno-MINTYPMAXDLY"],
        timing_loop => 1
        );

    execute(
        check_finished => 1,
        );

    vcd_identical($Self->trace_filename, $Self->{golden_filename});
}

ok(1);
1;
