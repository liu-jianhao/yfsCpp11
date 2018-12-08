#!/usr/bin/perl -w

# Case 1: Single file system
# 1. create new files
# 2. open an existing file
# 3. write to a file
#      - write to the middle
#      - append
#      - write from middle and beyond file length
#      - seek beyond file length and write
# 4. read file content
#      from each case above
#
# Make sure file handles/file type for new files are correct.

# Case 2: Two file systems mounted under same root dir.
# 0. Start two fs with same rootdir
# 1. create files in dir1
# 2. read the files from dir2
#
#

use strict;
$| = 1;

if($#ARGV != 1){
    print STDERR "Usage: test-lab-2-b.pl directory1 directory2\n";
    exit(1);
}
my $dir1 = $ARGV[0];
my $dir2 = $ARGV[1];

my $f1 = "a$$";
my $f2 = "b$$";

my $files = { };

print "Write and read one file: ";
writeone($dir1, $f1, 600);
checkcontent($dir1, $f1);
print "OK\n";

print "Write and read a second file: ";
writeone($dir1, $f2, 4111);
checkcontent($dir1, $f2);
checkcontent($dir1, $f1);
print "OK\n";

print "Overwrite an existing file: ";
writeone($dir1, $f1, 275); # shorter than before...
checkcontent($dir1, $f1);
checkcontent($dir1, $f2);
print "OK\n";

print "Append to an existing file: ";
writeone($dir1, $f1, 8192);
append($dir1, $f1, 7007);
checkcontent($dir1, $f1);
print "OK\n";

print "Write into the middle of an existing file: ";
writeat($dir1, $f1, 190);
checkcontent($dir1, $f1);
print "OK\n";

print "Write beyond the end of an existing file: ";
writeat($dir1, $f1, 65536);
checkcontent($dir1, $f1);
print "OK\n";

print "Check that one cannot open non-existant file: ";
checknot($dir1, "z-$$-z");
print "OK\n";

print "Check directory listing: ";
dircheck($dir1);
print "OK\n";

print "Read files via second server: ";
checkcontent($dir2, $f1);
checkcontent($dir2, $f2);
print "OK\n";

print "Check directory listing on second server: ";
dircheck($dir2);
print "OK\n";

print "Passed all tests\n";

sub writeone {
    my($d, $name, $len) = @_;
    my $contents = "";

    my $f = $d . "/" . $name;

    use FileHandle;
    sysopen F, $f, O_TRUNC|O_RDWR|O_CREAT
	or die "cannot create $f\n";

    while(length($contents) < $len){
	$contents .= rand();
    }
    $contents = substr($contents, 0, $len);
    $files->{$name} = $contents;

    syswrite F, $files->{$name}, length($files->{$name}) 
	or die "cannot write to $f";
    close(F);
}

sub checkcontent {
    my($d, $name) = @_;

    my $f = $d . "/" . $name;

    open F, "$f" or die "could not open $f for reading";
    my $c2 = "";
    while(<F>) {
      $c2 .= $_;
    }
    close(F);
    $files->{$name} eq $c2 or die "content of $f is incorrect\n";
}

sub checknot {
    my($d, $name) = @_;

    my $f = $d . "/" . $name;

    my $x = open(F, $f);
    if(defined($x)){
        print STDERR "$x exists but should not\n";
        exit(1);
    }
}

sub append {
    my($d, $name, $n) = @_;

    my $f = $d . "/" . $name;

    use FileHandle;
    sysopen F, "$f", O_RDWR
	or die "cannot open $f for append\n";

    my $contents = "";
    while(length($contents) < $n){
	$contents .= rand();
    }
    $contents = substr($contents, 0, $n);
    $files->{$name} .= $contents; ## Append the file content
    
    seek(F, 0, 2);  ## goto end of file
    syswrite(F, $contents, length($contents), 0) or die "cannot append to $f";
    close(F);
}

sub writeat {
    my($d, $name, $off) = @_;

    my $f = $d . "/" . $name;

    use FileHandle;
    sysopen F, "$f", O_RDWR
	or die "cannot open $f for read/write\n";

    my $contents = rand();

    my $x = $files->{$name};
    if (length($x) < $off + length($contents)) {
      my $nappend = $off + length($contents) - length($x);
      for (my $i=0; $i < $nappend; $i++) {
        $x .= "\0";
      }
    }
    substr($x, $off, length($contents)) = $contents;
    $files->{$name} = $x;
    
    seek(F, $off, 0);
    syswrite(F, $contents, length($contents), 0)
        or die "cannot write $f at offset $off";
    close(F);
}

sub dircheck {
    my($dir) = @_;

    opendir(D, $dir);
    my %h;
    my $f;
    while(defined($f = readdir(D))){
        if(defined($h{$f})){
            print STDERR "$f appears more than once in directory $dir\n";
            exit(1);
        }
        $h{$f} = 1;
    }
    closedir(D);

    foreach $f (keys(%$files)){
        if(!defined($h{$f})){
            print STDERR "$f is missing from directory $dir\n";
            exit(1);
        }
    }
}

exit(0);

