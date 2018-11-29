#!/usr/bin/perl -w

# test CREATE/MKNOD, LOOKUP, READDIR.

use strict;

if($#ARGV != 0){
    print STDERR "Usage: test-lab-2-a.pl directory\n";
    exit(1);
}
my $dir = $ARGV[0];

my $seq = 0;

my $files = { };
my @dead;

for(my $iters = 0; $iters < 200; $iters++){
    createone();
}

for(my $iters = 0; $iters < 100; $iters++){
    if(rand() < 0.1){
        livecheck();
    }
    if(rand() < 0.1){
        deadcheck();
    }
    if(rand() < 0.02){
        dircheck();
    }
    if(rand() < 0.5){
        createone();
    }
}

dircheck();
printf "Passed all tests!\n";
exit(0);

sub createone {
    my $name = "file-";
    for(my $i = 0; $i < 40; $i++){
	$name .= sprintf("%c", ord('a') + int(rand(26)));
    }
    $name .= "-$$-" . $seq;
    $seq = $seq + 1;
    my $contents = rand();
    print "create $name\n";
    if(!open(F, ">$dir/$name")){
        print STDERR "test-lab-2-a: cannot create $dir/$name : $!\n";
        exit(1);
    }
    close(F);
    $files->{$name} = $contents;
}

sub createagain {
    my @a = keys(%$files);
    return if $#a < 0;
    my $i = int(rand($#a + 1));
    my $k = $a[$i];
    print "re-create $k\n";
    if(!open(F, ">$dir/$k")){
        print STDERR "test-lab-2-a: cannot re-create $dir/$k : $!\n";
        exit(1);
    }
    close(F);
}

# make sure all the live files are there,
# and that all the dead files are not there.
sub dircheck {
    print "dircheck\n";
    opendir(D, $dir);
    my %h;
    my $f;
    while(defined($f = readdir(D))){
        if(!defined($h{$f})){
            $h{$f} = 0;
        }
        $h{$f} = $h{$f} + 1;
    }
    closedir(D);

    foreach $f (keys(%$files)){
        if(!defined($h{$f})){
            print STDERR "test-lab-2-a.pl: $f is not in the directory listing\n";
            exit(1);
        }
        if($h{$f} > 1){
            print STDERR "test-lab-2-a.pl: $f appears more than once in the directory\n";
            exit(1);
        }
    }

    foreach $f (@dead){
        if(defined($h{$f})){
            print STDERR "test-lab-2-a.pl: $f is dead but in directory listing\n";
            exit(1);
        }
    }
}

sub livecheck {
    my @a = keys(%$files);
    return if $#a < 0;
    my $i = int(rand($#a + 1));
    my $k = $a[$i];
    print "livecheck $k\n";
    if(!open(F, "$dir/$k")){
        print STDERR "test-lab-2-a: cannot open $dir/$k : $!\n";
        exit(1);
    }
    close(F);
    if( ! -f "$dir/$k" ){
	print STDERR "test-lab-2-a: $dir/$k is not of type file\n";
	exit(1);
    }
    if(open(F, ">$dir/$k/xx")){
	print STDERR "test-lab-2-a: $dir/$k acts like a directory, not a file\n";
        exit(1);
    }
}

sub deadcheck {
    my $name = "file-$$-" . $seq;
    $seq = $seq + 1;
    print "check-not-there $name\n";
    if(open(F, "$dir/$name")){
        print STDERR "test-lab-2-a: $dir/$name exists but should not\n";
        exit(1);
    }
}

sub deleteone {
    my @a = keys(%$files);
    return 0 if $#a < 0;
    my $i = int(rand($#a + 1));
    my $k = $a[$i];
    print "delete $k\n";
    if(unlink($dir . "/" . $k) == 0){
        print STDERR "test-lab-2-a: unlink $k failed: $!\n";
        exit(1);
    }
    delete $files->{$k};
    push(@dead, $k);
    return 1;
}
