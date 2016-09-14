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
my $htmlroot;

$Data::Dumper::Indent = 1;
$Data::Dumper::Sortkeys = 1;

select STDERR;
$| = 1;
select STDOUT;
$| = 1;

sub usage
{
    print STDERR <<WOOF;
Usage: check-links.pl [options] htmlroot

Options:
    -u  warn about unused anchors
    -v  verbose
    -d  debugging mode (implies -v)
WOOF
    exit 1;
}

sub out_warn
{
    my ($file, $message) = @_;

    $counts{'warning'}->{$file} ++;
    print STDERR "$file: warning: $message\n";
}

sub out_error
{
    my ($file, $message) = @_;

    $counts{'error'}->{$file} ++;
    print STDERR "$file: error: $message\n";
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
    my ($files, $dir, $exts) = @_;
    $exts = [ 'html' ] if not ref $exts;

    my $cb = sub {
        return if not m{\.([^.]+)$};
        return if not $1;
        return if not grep { $_ eq $1 } @{$exts};
        $files->{abs_path($_)} = { name => $File::Find::name };
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
            out_error $data->{name}, "duplicate id: $id" if exists $data->{anchors}->{$id};
            $data->{anchors}->{$id} = {};
        }

        if ($tag->is_tag('a')) {
            my $name = $tag->get_attr('name');
            if ($name) {
                out_error $data->{name}, "duplicate anchor: $name" if exists $data->{anchors}->{$name};
                $data->{anchors}->{$name} = {};
            }
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

        out_verbose "$filename: checking links...";
        my (undef, $path, undef) = fileparse($filename);

        foreach my $href (@{$files->{$real_filename}->{hrefs}}) {
            my ($link, $anchor, $junk) = split /#/, $href;
            out_error $filename, "bad link: $href" if $junk;

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

                if (-f $link) {
                    $key = abs_path($link);

                    if (exists $files->{$key}) {
                        $files->{$key}->{seen} ++;
                        out_debug "seen $link: $files->{$key}->{seen}";
                    }
                    else {
                        # XXX hush noise from links to non-html files
#                       out_warn $filename, "link to unrecognised file: $href ($link)";
                    }
                }
                else {
                    out_error $filename, "link to nonexistent file: $href ($link)" if not -f $link;
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
                    out_error $filename, "link to unrecognised anchor: $href ($anchor)";
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

        out_verbose "$filename: checking what links to me...";

        out_error $filename, "nothing links to me" if not $files->{$real_filename}->{seen};

        next if not $options{u};
        while (my ($id, $anchor) = each %{$files->{$real_filename}->{anchors}}) {
            out_warn $filename, "nothing links to #$id" if not $anchor->{seen};
        }
    }
}

sub summarise
{
    return if not $options{v};

    my ($counts) = @_;

    if (scalar keys %{$counts->{error}}) {
        out_verbose "Errors:";
        foreach my $f (sort keys %{$counts->{error}}) {
            out_verbose "    $f: $counts->{error}->{$f}";
        }
    }

    if (scalar keys %{$counts->{warning}}) {
        out_verbose "Warnings:";
        foreach my $f (sort keys %{$counts->{warning}}) {
            out_verbose "    $f: $counts->{warning}->{$f}";
        }
    }
}

#############################################################################

usage if not getopts("duv", \%options);
$htmlroot = shift @ARGV // q{.};

my %files;
find_files(\%files, $htmlroot);

# parse the files
while (my ($file, $data) = each %files) {
    out_verbose "parsing $data->{name}...";
    parse_file($file, $data);
}

# process our discoveries
check_hrefs(\%files);
check_seen(\%files);

# and wrap up
summarise(\%counts);
exit scalar keys %{$counts{error}};
