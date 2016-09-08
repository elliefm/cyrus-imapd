#!/usr/bin/perl
#
# Copyright (c) 1994-2016 Carnegie Mellon University.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
#
# 3. The name "Carnegie Mellon University" must not be used to
#    endorse or promote products derived from this software without
#    prior written permission. For permission or any legal
#    details, please contact
#      Carnegie Mellon University
#      Center for Technology Transfer and Enterprise Creation
#      4615 Forbes Avenue
#      Suite 302
#      Pittsburgh, PA  15213
#      (412) 268-7393, fax: (412) 268-7395
#      innovation@andrew.cmu.edu
#
# 4. Redistributions of any form whatsoever must retain the following
#    acknowledgment:
#    "This product includes software developed by Computing Services
#     at Carnegie Mellon University (http://www.cmu.edu/computing/)."
#
# CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO
# THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
# AND FITNESS, IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY BE LIABLE
# FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
# AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
# OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#

use strict;
use warnings;

use Data::Dumper;
use File::Basename;
use File::Find;
use Getopt::Std;
use HTML::TokeParser::Simple;

my %options;
my %counts;
my %files;
my $htmlroot;

sub usage
{
    print STDERR "Usage: check-links.pl [options] htmlroot\n";
    exit 1;
}

sub find_files
{
    my ($dir, $exts) = @_;
    $exts = [ 'html' ] if not ref $exts;

    my @files;

    my $wanted = sub {
        my $ext = (split /\./, $_)[-1];
        return if not $ext;
        return if not grep { $_ eq $ext } @{$exts};
        push @files, $File::Find::name;
    };

    find($wanted, $dir);

    return @files;
}

sub out_warn
{
    my ($message, $count_str) = @_;

    $counts{'warning'} ++;
    $counts{$count_str} ++ if $count_str;
    print STDERR 'warning: ', $message, "\n";
}

sub out_error
{
    my ($message, $count_str) = @_;

    $counts{'error'} ++;
    $counts{$count_str} ++ if $count_str;
    print STDERR 'error: ', $message, "\n";
}

sub out_verbose
{
    return if not $options{v} and not $options{d};

    my ($message) = @_;
    print $message, "\n";
}

sub out_debug
{
    return if not $options{d};

    my ($message) = @_;
    print $message, "\n";
}

sub parse_file
{
    my ($filename, $data) = @_;

    my $p = HTML::TokeParser::Simple->new(file => $filename);

    while (my $tag = $p->get_tag) {
        next if not $tag->is_start_tag();

        my $id = $tag->get_attr('id');
        if ($id) {
            out_error "$filename: duplicate id: $id" if exists $data->{anchors}->{$id};
            $data->{anchors}->{$id} = {};
        }

        my $href = $tag->get_attr('href');
        push @{$data->{hrefs}}, $href if $href;
    }
}

sub check_hrefs
{
    my ($files) = @_;

    foreach my $filename (sort keys %{$files}) {
        out_verbose "checking $filename hrefs...";
        my (undef, $path, undef) = fileparse($filename);
        out_debug "$filename is in $path";

        foreach my $href (@{$files->{$filename}->{hrefs}}) {
            my ($link, $anchor, $junk) = split /#/, $href;
            out_error "bad link: $href" if $junk;

            if ($link) {
                # skip mailto links
                next if $link =~ m{^mailto:};

                # XXX todo - check external links
                next if $link =~ m{^\w+://};

                if ($link =~ m{^/}) {
                    $link = "$htmlroot/$link";
                }
                else {
                    $link = "$path/$link";
                }

                out_error "$filename: link to nonexistent file: $href ($link)" if not -f $link;
            }

            # XXX fix the link path so we can find it in our hash
            # XXX then mark it as seen
            # XXX and if we're linking to an anchor, make sure that anchor exists
            # XXX and mark it as soon too
        }
    }
}

#############################################################################

usage if not getopts("dv", \%options);
$htmlroot = shift @ARGV // q{.};

out_debug Dumper \%options;

out_debug "htmlroot: $htmlroot";
out_debug "ok";

%files = map { ($_ => {}) } find_files $htmlroot;

#print Dumper \%files;

# parse the files
while (my ($file, $data) = each %files) {
    out_verbose "parsing $file...";
    parse_file($file, $data);
}

out_debug Dumper \%files;

# process our discoveries
check_hrefs(\%files);

out_debug Dumper \%counts;
