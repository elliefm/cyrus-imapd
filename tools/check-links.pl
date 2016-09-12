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

use Cwd qw(abs_path);
use Data::Dumper;
use File::Basename;
use File::Find;
use Getopt::Std;
use HTML::TokeParser::Simple;

my %options;
my %counts;
my %g_files;
my $htmlroot;

$Data::Dumper::Indent = 1;
$Data::Dumper::Sortkeys = 1;

sub usage
{
    print STDERR "Usage: check-links.pl [options] htmlroot\n";
    exit 1;
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

sub find_files
{
    my ($dir, $exts) = @_;
    $exts = [ 'html' ] if not ref $exts;

    my $cb = sub {
        my $ext = (split /\./, $_)[-1];
        return if not $ext;
        return if not grep { $_ eq $ext } @{$exts};
        $g_files{abs_path($_)} = { name => $File::Find::name };
    };

    find($cb, $dir);
}

sub parse_file
{
    my ($filename, $data) = @_;

    my $p = HTML::TokeParser::Simple->new(file => $filename);

    while (my $tag = $p->get_tag) {
        next if not $tag->is_start_tag();

        my $id = $tag->get_attr('id');
        if ($id) {
            out_error "$data->{name}: duplicate id: $id" if exists $data->{anchors}->{$id};
            $data->{anchors}->{$id} = {};
        }

        my $href = $tag->get_attr('href');
        push @{$data->{hrefs}}, $href if $href;

        if ($tag->is_tag('form')) {
            my $action = $tag->get_attr('action');
            push @{$data->{hrefs}}, $action if $action;
        }
    }
}

sub check_hrefs
{
    my ($files) = @_;

    foreach my $real_filename (sort keys %{$files}) {
        my $filename = $files->{$real_filename}->{name};

        out_verbose "checking $filename hrefs...";
        my (undef, $path, undef) = fileparse($filename);
        out_debug "$filename is in $path";

        foreach my $href (@{$files->{$real_filename}->{hrefs}}) {
            my ($link, $anchor, $junk) = split /#/, $href;
            out_error "bad link: $href" if $junk;

            my $key;

            if ($link) {
                # skip mailto links
                next if $link =~ m{^mailto:};

                # XXX todo - check external links
                next if $link =~ m{^\w+://};

                if ($link =~ m{^/}) {
                    $link = "$htmlroot/$link";
                }
                else {
                    $link = "$path$link"; # $path has trailing '/' already!
                }

                out_error "$filename: link to nonexistent file: $href ($link)" if not -f $link;

                $key = abs_path($link);

                if (exists $files->{$key}) {
                    $files->{$key}->{seen} ++;
                    out_debug "seen $link: $files->{$key}->{seen}";
                }
                else {
                    # XXX hush noise from links to non-html files
#                    out_warn "$filename: link to unrecognised file: $href ($link)";
                }
            }
            else {
                # no link, so links to anchors are within the same page
                $key = $real_filename;
            }

            if ($anchor) {
                if (exists $files->{$key}->{anchors}->{$anchor}) {
                    $files->{$key}->{anchors}->{$anchor}->{seen}++;
                    out_debug "seen #$anchor: $files->{$key}->{anchors}->{$anchor}->{seen}";
                }
                else {
                    out_error "$filename: link to unrecognised anchor: $href ($anchor)";
                }
            }
        }
    }
}

sub check_seen
{
    my ($files) = @_;

    foreach my $real_filename (sort keys %{$files}) {
        my $filename = $files->{$real_filename}->{name};

        out_verbose "checking $filename seen...";

        out_error "$filename: nothing links to me" if not $files->{$real_filename}->{seen};

        while (my ($id, $anchor) = each %{$files->{$real_filename}->{anchors}}) {
            out_warn "$filename: unused anchor '$id'" if not $anchor->{seen};
        }
    }
}

#############################################################################

usage if not getopts("dv", \%options);
$htmlroot = shift @ARGV // q{.};

out_debug Dumper \%options;

out_debug "htmlroot: $htmlroot";
out_debug "ok";

find_files $htmlroot;

#print Dumper \%files;

# parse the files
while (my ($file, $data) = each %g_files) {
    out_verbose "parsing $data->{name}...";
    parse_file($file, $data);
}


# process our discoveries
check_hrefs(\%g_files);
out_debug Dumper \%g_files;
check_seen(\%g_files);

out_debug Dumper \%counts;
