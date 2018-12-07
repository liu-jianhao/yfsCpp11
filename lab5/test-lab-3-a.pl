#!/usr/bin/perl -w

sub oops {
    my($msg) = @_;
    print STDERR "test-lab-3-a.pl error: $msg : $!\n";
    exit(1);
}

sub oops1 {
    my($msg) = @_;
    print STDERR "test-lab-3-a.pl error: $msg\n";
    exit(1);
}

if($#ARGV != 0){
    print STDERR "Usage: test-lab-3-a.pl directory\n";
    exit(1);
}

my $seq = 0;
my $root = $ARGV[0];
my $dir = $root . "/d" . $$;
print "mkdir $dir\n";
if(mkdir($dir, 0777) == 0){
    oops("mkdir $dir");
}

my $files = { };
my @dead;

createone();
deleteone();
createone();
checkmtime();
checkdirmtime();

for($iters = 0; $iters < 10; $iters++){
    createone();
}

for($iters = 0; $iters < 50; $iters++){
    if(rand() < 0.2){
        deadcheck();
    }
    if(rand() < 0.2){
        livecheck();
    }
    if(rand() < 0.02){
        dircheck();
    }
    if(rand() < 0.1){
        checkdirmtime();
    }
    if(rand() < 0.1){
        checkmtime();
    }
    if(rand() < 0.5){
        createone();
    }
    if(rand() < 0.5){
        deleteone();
    }
}

dircheck();
cleanup();
dircheck();
printf "Passed all tests!\n";
exit(0);

sub createone {
    my $name = "x-" . $seq;
    $seq = $seq + 1;
    my $contents = rand();
    print "create $name\n";
    if(!open(F, ">$dir/$name")){
        oops("cannot create $name");
    }
    print F "$contents";
    close(F);
    $files->{$name} = $contents;
}

# make sure all the live files are there,
# and that all the dead files are not there.
sub dircheck {
    print "dircheck\n";
    opendir(D, $dir);
    my %h;
    my $f;
    while(defined($f = readdir(D))){
        if(defined($h{$f})){
            oops1("$f occurs twice in directory");
        }
        $h{$f} = 1;
    }
    closedir(D);

    foreach $f (keys(%$files)){
        if(!defined($h{$f})){
            oops1("$f is not in directory listing");
        }
    }

    foreach $f (@dead){
        if(defined($h{$f})){
            oops1("$f was removed but is in directory listing");
        }
    }

    foreach $f (keys(%h)){
        next if ($f eq "." or $f eq "..");
        if(!defined($files->{$f})){
            oops1("unexpected file $f in directory listing");
        }
    }
}

sub livecheck {
    my @a = keys(%$files);
    return if $#a < 0;
    my $i = int(rand($#a + 1));
    my $k = $a[$i];
    print "livecheck $k\n";
    oops("cannot open $k") if !open(F, "$dir/$k");
    my $z = <F>;
    if($z ne $files->{$k}){
        oops1("file $k wrong contents");
    }
    close(F);
}

sub deadcheck {
    return if $#dead < 0;
    my $i = int(rand($#dead + 1));
    my $k = $dead[$i];
    return if defined($files->{$k}); # ???
    print "deadcheck $k\n";
    if(rand(1.0) < 0.5){
        if(open(F, $dir . "/" . $k)){
            oops1("dead file $k is readable");
        }
    } else {
        if(unlink($dir . "/" . $k)){
            oops1("dead file $k was removable");
        }
    }
}

sub deleteone {
    my @a = keys(%$files);
    return 0 if $#a < 0;
    my $i = int(rand($#a + 1));
    my $k = $a[$i];
    print "delete $k\n";
    if(unlink($dir . "/" . $k) == 0){
        oops("unlink $k failed");
    }
    delete $files->{$k};
    push(@dead, $k);
    return 1;
}

sub checkdirmtime {
    print "checkdirmtime\n";
    opendir(D, $dir);
    closedir(D);
    my @st1 = stat($dir . "/.");
    sleep(2);
    my $op;
    if(rand() < 0.75){
        return if deleteone() == 0;
        $op = "delete";
    } else {
        createone();
        $op = "create";
    }
    opendir(D, $dir);
    closedir(D);
    my @st2 = stat($dir . "/.");
    if($st1[9] == $st2[9]){
	print $st2[9], " ", $st2[9], "\n";
        oops1("$op did not change directory mtime");
    }
    if($st1[10] == $st2[10]){
        oops1("$op did not change directory ctime");
    }
}

sub checkmtime {
    my @a = keys(%$files);
    return if $#a < 0;
    my $i = int(rand($#a + 1));
    my $k = $a[$i];
    print "checkmtime $k\n";

    my @st1 = stat("$dir/$k");
    sleep(2);
    if(!open(F, ">$dir/$k")){
        oops("cannot re-create $dir/$k");
    }
    my @st2 = stat("$dir/$k");
    sleep(2);
    print F $files->{$k};
    close(F);
    if(!open(F, "$dir/$k")){
        oops("cannot open $dir/$k");
    }
    close(F);
    my @st3 = stat("$dir/$k");
    
    if($st1[9] == $st2[9]){
        oops1("CREATE did not change mtime");
    }
    if($st2[9] == $st3[9]){
        oops1("WRITE did not change mtime");
    }
}

sub cleanup {
    while(deleteone()){
    }
}
