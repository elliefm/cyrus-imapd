#!/usr/bin/perl
#
#  Copyright (c) 2011-2024 FastMail Pty Ltd. All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#
#  3. The name "Fastmail Pty Ltd" must not be used to
#     endorse or promote products derived from this software without
#     prior written permission. For permission or any legal
#     details, please contact
#      FastMail Pty Ltd
#      PO Box 234
#      Collins St West 8007
#      Victoria
#      Australia
#
#  4. Redistributions of any form whatsoever must retain the following
#     acknowledgment:
#     "This product includes software developed by Fastmail Pty. Ltd."
#
#  FASTMAIL PTY LTD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
#  INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY  AND FITNESS, IN NO
#  EVENT SHALL OPERA SOFTWARE AUSTRALIA BE LIABLE FOR ANY SPECIAL, INDIRECT
#  OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
#  USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
#  TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
#  OF THIS SOFTWARE.
#

package Cassandane::Unit::RunnerJSON;
use strict;
use warnings;
use Data::Dumper;
use JSON;

use lib '.';
use base qw(Cassandane::Unit::Runner);

sub new
{
    my ($class, $params, @args) = @_;
    my $self = $class->SUPER::new(@args);
    $self->{_ok} = [];
    return $self;
}

sub _getname
{
    my $test = shift;
    my $suite = ref($test);
    $suite =~ s/^Cassandane:://;

    my $testname = $test->{"Test::Unit::TestCase_name"};
    $testname =~ s/^test_//;

    return "$suite.$testname";
}

sub start_test
{
    my ($self, $test) = @_;
    # suppress default output
}

sub add_pass
{
    my ($self, $test) = @_;

    push @{$self->{_ok}}, _getname($test);
}

sub add_error
{
    my ($self, $test) = @_;

    $self->record_failed($test);
    # suppress default output
}

sub add_failure
{
    my ($self, $test) = @_;

    $self->record_failed($test);
    # suppress default output
}

sub print_result
{
    my ($self, $result, $start_time, $end_time) = @_;

    my $report = {
        ok => [ sort @{$self->{_ok}} ],
        error => [],
        failed => [],
        backtrace => {},
        annotations => {},
    };

    foreach my $e (@{$result->errors()}, @{$result->failures()}) {
        my $type = ref $e;
        my $test = $e->object();
        my $name = _getname($test);

        if ($type eq 'Test::Unit::Failure') {
            push @{$report->{failed}}, $name;
        }
        else {
            if ($type ne 'Test::Unit::Error') {
                warn "weird exception: " . Dumper $e;
            }
            push @{$report->{error}}, $name;
        }

        my (undef, $backtrace) = split(/\n/, $e->to_string(), 2);
        $report->{backtrace}->{$name} = [
            split /\n/, $backtrace
        ];

        $report->{annotations}->{$name} = [
            split /\n/, $test->annotations()
        ]
    }

    my $json = JSON->new();
    $json->utf8();
    $json->pretty();
    $self->_print($json->encode($report));
}

sub do_run {
    my ($self, $suite, $wait) = @_;
    my $result = $self->create_test_result();
    $result->add_listener($self);
    my $start_time = new Benchmark();
    $suite->run($result, $self);
    my $end_time = new Benchmark();

    $self->print_result($result, $start_time, $end_time);

    # suppress default wait/not successful handling

    return $result->was_successful;
}

1;
