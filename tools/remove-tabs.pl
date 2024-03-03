#!/usr/bin/perl -w

use IO::File;
use IO::Dir;

my $name = shift || '.';
cleanup($name);

sub cleanup {
  my $name = shift;
  return cleanup_file($name) if -f $name;
  my $d = IO::Dir->new($name);
  while (defined($_ = $d->read)) {
    next if m/^\./;

    my $path = "$name/$_";
    next if -l $path;  # don't mess with symlinks

    if (-f $path) {
      my @want = (
        qr{\.c$},
        qr{\.cpp$},
        qr{\.h$},
        qr{\.pl$},
        qr{\.pm$},
        qr{\.testc$},
      );

      my $found = 0;
      foreach my $p (@want) {
        $found += scalar m/$p/;
      }
      next if not $found;
    }

    cleanup($path);
  }
}


sub cleanup_file {
  my $filename = shift;
  print "$filename\n";
  my $ih = IO::File->new($filename, "r") || die "can't read $filename";
  my $oh = IO::File->new("$filename.new", "w");

  if (stream_clean($ih, $oh)) {
    system("chmod", "a+x", "$filename.new") if -x $filename;
    rename("$filename.new", "$filename");
  }
  else {
    unlink("$filename.new");
  }
}

sub stream_clean {
  my ($ih, $oh) = @_;
  while (<$ih>) {
    print $oh clean_line($_) . "\n";
  }
  return 1;
}

sub clean_line {
  my $line = shift;
  use bytes;
  $line =~ s/[ \t]+$//;
  $line =~ s/[\r\n]//g;
  my $op = 0;
  my $out = "";
  foreach my $i (0..(length($line)-1)) {
    my $chr = substr($line, $i, 1);
    if ($chr eq "\t") {
      my $inc = 8 - ($op % 8);
      $out .= " " x $inc;
      $op += $inc;
    }
    else {
      $out .= $chr;
      $op++;
    }
  }
  return $out;
}
