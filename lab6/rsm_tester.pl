#!/usr/bin/perl -w

use POSIX ":sys_wait_h";
use Getopt::Std;
use strict;


my @pid;
my @logs = ();
my @views = (); #expected views
my %in_views; #the number of views a node is expected to be present
my @p;
my $t;
my $always_kill = 0;

use sigtrap 'handler' => \&killprocess, 'HUP', 'INT', 'ABRT', 'QUIT', 'TERM';

sub paxos_log {
  my $port = shift;
  return "paxos-$port.log";
}

sub mydie {
  my ($s) = @_;
  killprocess() if ($always_kill);
  die $s;
}

sub killprocess {
  print "killprocess: forcestop all spawned processes...@pid \n";
  kill 9, @pid;
}

sub cleanup {
  kill 9, @pid;
  unlink(@logs);
  sleep 2;
}

sub spawn {
  my ($p, @a) = @_;
  my $aa = join("-", @a);
  if (my $pid = fork) {
# parent
    push( @logs, "$p-$aa.log" );
    if( $p =~ /config_server/ ) {
      push( @logs, paxos_log($a[1]) );
    }
    if( $p =~ /lock_server/ ) {
      push( @logs, paxos_log($a[1]) );
    }
    return $pid;
  } elsif (defined $pid) {
# child
    open(STDOUT, ">>$p-$aa.log")
      or mydie "Couln't redirect stout\n";
    open(STDERR, ">&STDOUT")
      or mydie "Couln't redirect stderr\n";
    $| = 1;
    print "$p @a\n";
    exec "$p @a" 
      or mydie "Cannot start new $p @a $!\n";
  } else {
    mydie "Cannot  fork: $!\n";
  }
}

sub randports {

  my $num = shift;
  my @p = ();
  for( my $i = 0; $i < $num; $i++ ) {
    push( @p, int(rand(54000))+10000 );
  }
  my @sp = sort { $a <=> $b } @p;
  return @sp;
}

sub print_config {
  my @ports = @_;
  open( CONFIG, ">config" ) or mydie( "Couldn't open config for writing" );
  foreach my $p (@ports) {
    printf CONFIG "%05d\n", $p;
  }
  close( CONFIG );
}

sub spawn_ls {
  my $master = shift;
  my $port = shift;
  return spawn( "./lock_server", $master, $port );
}

sub spawn_config {
  my $master = shift;
  my $port = shift;
  return spawn( "./config_server", $master, $port );
}

sub check_views {

  my $l = shift;
  my $v = shift;
  my $last_v = shift;

  open( LOG, "<$l" ) 
    or mydie( "Failed: couldn't read $l" );
  my @log = <LOG>;
  close(LOG);

  my @vs = @{$v};

  my $i = 0;
  my @last_view;
  foreach my $line (@log) {
    if( $line =~ /^done (\d+) ([\d\s]+)$/ ) {

      my $num = $1;
      my @view = split( /\s+/, $2 );
      @last_view = @view;

      if( $i > $#vs ) {
# let there be extra views
        next;
      }

      my $e = $vs[$i];
      my @expected = @{$e};

      if( @expected != @view ) {
        mydie( "Failed: In log $l at view $num is (@view), but expected $i (@expected)" );
      }

      $i++;
    }
  }

  if( $i <= $#vs ) {
    mydie( "Failed: In log $l, not enough views seen!" );
  }

  if( defined $last_v ) {
    my @last_exp_v = @{$last_v};
    if( @last_exp_v != @last_view ) {
      mydie( "Failed: In log $l last view didn't match, got view @last_view, but expected @last_exp_v" );
    }
  }

}

sub get_num_views {

  my $log = shift;
  my $including = shift;
  my $nv = `grep "done " $log | grep "$including" | wc -l`;
  chomp $nv;
  return $nv;

}

sub wait_for_view_change {

  my $log = shift;
  my $num_views = shift;
  my $including = shift;
  my $timeout = shift;

  my $start = time();
  while( (get_num_views( $log, $including ) < $num_views) and
      ($start + $timeout > time()) ) {
		my $lastv = `grep done  $log | tail -n 1`;
		chomp $lastv;
    print "   Waiting for $including to be present in >=$num_views views in $log (Last view: $lastv)\n";
    sleep 1;
  }

  if( get_num_views( $log, $including ) < $num_views) {
    mydie( "Failed: Timed out waiting for $including to be in >=$num_views in log $log" );
  }else{
    print "   Done: $including is in >=$num_views views in $log\n";
  }
}

sub waitpid_to {
  my $pid = shift;
  my $to = shift;

  my $start = time();
  my $done_pid;
  do {
    sleep 1;
    $done_pid = waitpid($pid, POSIX::WNOHANG);
  } while( $done_pid <= 0 and (time() - $start) < $to );

  if( $done_pid <= 0 ) {
    kill 9,$pid;
    mydie( "Failed: Timed out waiting for process $pid\n" );
  } else {
    return 1;
  }

}

sub wait_and_check_expected_view($) {
  my $v = shift;
  push @views, $v;
  for (my $i = 0; $i <=$#$v; $i++) {
    $in_views{$v->[$i]}++;
  }
  foreach my $port (@$v) {
    wait_for_view_change(paxos_log($port), $in_views{$port}, $port, 20);
  }
  foreach my $port (@$v) {
    my $log = paxos_log($port);
    check_views( $log, \@views );
  }
}

sub start_nodes ($$){

  @pid = ();
  @logs = ();
  @views = ();
  for (my $i = 0; $i <= $#p; $i++) {
    $in_views{$p[$i]} = 0;
  }

  my $n = shift;
  my $command = shift;

  for (my $i = 0; $i < $n; $i++) {
		if ($command eq "ls") {
			@pid = (@pid, spawn_ls($p[0],$p[$i]));
			print "Start lock_server on $p[$i]\n";
		}elsif ($command eq "config_server"){
			@pid = (@pid, spawn_config($p[0],$p[$i]));
			print "Start config on $p[$i]\n";
		}
    sleep 1;

    my @vv = @p[0..$i];
    wait_and_check_expected_view(\@vv);
  }

}

my %options;
getopts("s:k",\%options);
if (defined($options{s})) {
  srand($options{s});
}
if (defined($options{k})) {
  $always_kill = 1;
}

#get a sorted list of random ports
@p = randports(5);
print_config( @p[0..4] );

my @do_run = ();
my $NUM_TESTS = 17;

# see which tests are set
if( $#ARGV > -1 ) {
  foreach my $t (@ARGV) {
    if( $t < $NUM_TESTS && $t >= 0 ) {
      $do_run[$t] = 1;
    }
  }
} else {
# turn on all tests
  for( my $i = 0; $i < $NUM_TESTS; $i++ ) {
    $do_run[$i] = 1;
  }
}

if ($do_run[0]) {
  print "test0: start 3-process lock server\n";
  start_nodes(3,"ls");
  cleanup();
  sleep 2;
}

if ($do_run[1]) {
  print "test1: start 3-process lock server, kill third server\n";
  start_nodes(3,"ls");

  print "Kill third server (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];
  
  sleep 5;

  # it should go through 4 views
  my @v4 = ($p[0], $p[1]);
  wait_and_check_expected_view(\@v4);
  
  cleanup();
  sleep 2;
}

if ($do_run[2]) {
  print "test2: start 3-process lock server, kill first server\n";
  start_nodes(3,"ls");
  
  print "Kill first (PID: $pid[0]) on port $p[0]\n";
  kill "TERM", $pid[0];

  sleep 5;

  # it should go through 4 views
  my @v4 = ($p[1], $p[2]);
  wait_and_check_expected_view(\@v4);

  cleanup();
  sleep 2;
}


if ($do_run[3]) {

  print "test3: start 3-process lock_server, kill a server, restart a server\n";
  start_nodes(3,"ls");

  print "Kill server (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  sleep 5;

  my @v4 = ($p[0], $p[1]);
  wait_and_check_expected_view(\@v4);

  print "Restart killed server on port $p[2]\n";
  $pid[2] = spawn_ls ($p[0], $p[2]);

  sleep 5;

  my @v5 = ($p[0], $p[1], $p[2]);
  wait_and_check_expected_view(\@v5);

  cleanup();
  sleep 2;
}

if ($do_run[4]) {
  print "test4: 3-process lock_server, kill third server, kill second server, restart third server, kill third server again, restart second server, re-restart third server, check logs\n";
  start_nodes(3,"ls");
  
  print "Kill server (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  sleep 5;
  my @v4 = ($p[0], $p[1]);
  wait_and_check_expected_view(\@v4);

  print "Kill server (PID: $pid[1]) on port $p[1]\n";
  kill "TERM", $pid[1];

  sleep 5;
  #no view change can happen because of a lack of majority

  print "Restarting server on port $p[2]\n";
  $pid[2] = spawn_ls($p[0], $p[2]);

  sleep 5;

  #no view change can happen because of a lack of majority
  foreach my $port (@p[0..2]) {
    my $num_v = get_num_views(paxos_log($port), $port);
    die "$num_v views in ", paxos_log($port), " : no new views should be formed due to the lack of majority\n" if ($num_v != $in_views{$port});
  }

  # kill node 3 again, 
  print "Kill server (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  sleep 5;


  print "Restarting server on port $p[1]\n";
  $pid[1] = spawn_ls($p[0], $p[1]);

  sleep 7;

  foreach my $port (@p[0..1]) {
    $in_views{$port} = get_num_views( paxos_log($port), $port );
    print "   Node $port is present in ", $in_views{$port}, " views in ", paxos_log($port), "\n";
  }

  print "Restarting server on port $p[2]\n";
  $pid[2] = spawn_ls($p[0], $p[2]);

  my @lastv = ($p[0],$p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

# now check the paxos logs and make sure the logs go through the right
# views

  foreach my $port (@lastv) {
    check_views( paxos_log($port), \@views, \@lastv);
  }

  cleanup();

}

if ($do_run[5]) {
  print "test5: 3-process lock_server, send signal 1 to first server, kill third server, restart third server, check logs\n";
  start_nodes(3,"ls");
  
  print "Sending paxos breakpoint 1 to first server on port $p[0]\n";
  spawn("./rsm_tester", $p[0]+1, "breakpoint", 3);

  sleep 1;

  print "Kill third server (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  sleep 5;
  foreach my $port (@p[0..2]) {
    my $num_v = get_num_views( paxos_log($port), $port );
    die "$num_v views in ", paxos_log($port), " : no new views should be formed due to the lack of majority\n" if ($num_v != $in_views{$port});
  }

  print "Restarting third server on port $p[2]\n";
  $pid[2]= spawn_ls($p[0], $p[2]);
  my @lastv = ($p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }
	sleep 10;

# now check the paxos logs and make sure the logs go through the right
# views

  foreach my $port (@lastv) {
    check_views( paxos_log($port), \@views, \@lastv);
  }

  cleanup();

}

if ($do_run[6]) {
  print "test6: 4-process lock_server, send signal 2 to first server, kill fourth server, restart fourth server, check logs\n";
  start_nodes(4,"ls");
  print "Sending paxos breakpoint 2 to first server on port $p[0]\n";
  spawn("./rsm_tester", $p[0]+1, "breakpoint", 4);

  sleep 1;

  print "Kill fourth server (PID: $pid[3]) on port $p[3]\n";
  kill "TERM", $pid[3];

  sleep 5;

  foreach my $port ($p[1],$p[2]) {
      my $num_v = get_num_views( paxos_log($port), $port );
      die "$num_v views in ", paxos_log($port), " : no new views should be formed due to the lack of majority\n" if ($num_v != $in_views{$port});
  }

  sleep 5;

  print "Restarting fourth server on port $p[3]\n";
  $pid[3] = spawn_ls($p[1], $p[3]);

  sleep 5;

  my @v5 = ($p[0],$p[1],$p[2]);
  foreach my $port (@v5) {
    $in_views{$port}++;
  }
  push @views, \@v5;

  sleep 10;

  # the 6th view will be (2,3)  or (1,2,3,4)
  my @v6 = ($p[1],$p[2]);
  foreach my $port (@v6) {
    $in_views{$port}++;
  }
  foreach my $port (@v6) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 30);
  }

  # final will be (2,3,4)
  my @lastv = ($p[1],$p[2],$p[3]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }
  foreach my $port (@lastv) {
    check_views( paxos_log($port), \@views, \@lastv );
  }
  cleanup();

}

if ($do_run[7]) {
  print "test7: 4-process lock_server, send signal 2 to first server, kill fourth server, kill other servers, restart other servers, restart fourth server, check logs\n";
  start_nodes(4,"ls");
  print "Sending paxos breakpoint 2 to first server on port $p[0]\n";
  spawn("./rsm_tester", $p[0]+1, "breakpoint", 4);
  sleep 3;

  print "Kill fourth server (PID: $pid[3]) on port $p[3]\n";
  kill "TERM", $pid[3];

  sleep 5;

  print "Kill third server (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  print "Kill second server (PID: $pid[1]) on port $p[1]\n";
  kill "TERM", $pid[1];

  sleep 5;

  print "Restarting second server on port $p[1]\n";
  $pid[1] = spawn_ls($p[0], $p[1]);

  sleep 5;

  print "Restarting third server on port $p[2]\n";
  $pid[2] = spawn_ls($p[0], $p[2]);

  sleep 5;

#no view change is possible by now because there is no majority
  foreach my $port ($p[1],$p[2]) {
    my $num_v = get_num_views( paxos_log($port), $port );
    die "$num_v views in ", paxos_log($port), " : no new views should be formed due to the lack of majority\n" if ($num_v != $in_views{$port});
  }

  print "Restarting fourth server on port $p[3]\n";
  $pid[3] = spawn_ls($p[1], $p[3]);

  sleep 5;

  my @v5 = ($p[0], $p[1], $p[2]);
  push @views, \@v5;
  foreach my $port (@v5) {
    $in_views{$port}++;
  }

  sleep 15;
  my @lastv = ($p[1],$p[2],$p[3]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  foreach my $port (@lastv) {
    check_views( paxos_log($port), \@views, \@lastv);
  }
  
  cleanup();

}

if ($do_run[8]) {
  print "test8: start 3-process lock service\n";
	start_nodes(3,"ls");
  
  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 8" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[9]) {

  print "test9: start 3-process rsm, kill second slave while lock_tester is running\n";
	start_nodes(3,"ls");
  
  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep int(rand(10)+1);

  print "Kill slave (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  sleep 3;

  # it should go through 4 views
  my @v4 = ($p[0], $p[1]);
  wait_and_check_expected_view(\@v4);

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 9" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[10]) {

  print "test10: start 3-process rsm, kill second slave and restarts it later while lock_tester is running\n";
	start_nodes(3,"ls");
  
  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep int(rand(10)+1);

  print "Kill slave (PID: $pid[2]) on port $p[2]\n";
  kill "TERM", $pid[2];

  sleep 3;

  # it should go through 4 views
  my @v4 = ($p[0], $p[1]);
  wait_and_check_expected_view(\@v4);

  sleep 3;

  print "Restarting killed lock_server on port $p[2]\n";
  $pid[2] = spawn_ls($p[0], $p[2]);
  my @v5 = ($p[0],$p[1],$p[2]);
  wait_and_check_expected_view(\@v5);

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 10" );
  }

  cleanup();
  sleep 2;
}


if ($do_run[11]) {

  print "test11: start 3-process rsm, kill primary while lock_tester is running\n";
	start_nodes(3,"ls");

  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep int(rand(10)+1);

  print "Kill primary (PID: $pid[0]) on port $p[0]\n";
  kill "TERM", $pid[0];

  sleep 3;

  # it should go through 4 views
  my @v4 = ($p[1], $p[2]);
  wait_and_check_expected_view(\@v4);

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 11" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[12]) {

  print "test12: start 3-process rsm, kill master at break1 and restart it while lock_tester is running\n";
  
  start_nodes(3, "ls");

  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep 1;

  print "Kill master (PID: $pid[0]) on port $p[0] at breakpoint 1\n";
  spawn("./rsm_tester", $p[0]+1, "breakpoint", 1);


  sleep 1;

  # it should go through 5 views
  my @v4 = ($p[1], $p[2]);
  wait_and_check_expected_view(\@v4);

  print "Restarting killed lock_server on port $p[0]\n";
  $pid[0] = spawn_ls($p[1], $p[0]);

  sleep 3;

  # the last view should include all nodes
  my @lastv = ($p[0],$p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  foreach my $port (@lastv) {
    check_views( paxos_log($port), \@views, \@lastv);
  }

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 12" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[13]) {

  print "test13: start 3-process rsm, kill slave at break1 and restart it while lock_tester is running\n";

  start_nodes(3, "ls");

  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep 1;

  print "Kill slave (PID: $pid[2]) on port $p[2] at breakpoint 1\n";
  spawn("./rsm_tester", $p[2]+1, "breakpoint", 1);

  sleep 1;

  # it should go through 4 views
  my @v4 = ($p[0], $p[1]);
  wait_and_check_expected_view(\@v4);

  print "Restarting killed lock_server on port $p[2]\n";
  $pid[2] = spawn_ls($p[0], $p[2]);

  sleep 3;

  # the last view should include all nodes
  my @lastv = ($p[0],$p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  foreach my $port (@lastv) {
    check_views( paxos_log($port), \@views, \@lastv);
  }

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 13" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[14]) {

  print "test14: start 5-process rsm, kill slave break1, kill slave break2\n";

  start_nodes(5, "ls");

  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep 1;

  print "Kill slave (PID: $pid[4]) on port $p[4] at breakpoint 1\n";
  spawn("./rsm_tester", $p[4]+1, "breakpoint", 1);


  print "Kill slave (PID: $pid[3]) on port $p[3] at breakpoint 2\n";
  spawn("./rsm_tester", $p[3]+1, "breakpoint", 2);


  sleep 1;

  # two view changes:

  print "first view change wait\n";
  my @lastv = ($p[0],$p[1],$p[2],$p[3]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  print "second view change wait\n";

  @lastv = ($p[0],$p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 14" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[15]) {

  print "test15: start 5-process rsm, kill slave break1, kill primary break2\n";

  start_nodes(5, "ls");

  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep 1;

  print "Kill slave (PID: $pid[4]) on port $p[4] at breakpoint 1\n";
  spawn("./rsm_tester", $p[4]+1, "breakpoint", 1);


  print "Kill primary (PID: $pid[0]) on port $p[0] at breakpoint 2\n";
  spawn("./rsm_tester", $p[0]+1, "breakpoint", 2);

  sleep 1;

  # two view changes:

  print "first view change wait\n";
  my @lastv = ($p[0],$p[1],$p[2],$p[3]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  print "second view change wait\n";

  @lastv = ($p[1],$p[2],$p[3]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 15" );
  }

  cleanup();
  sleep 2;
}

if ($do_run[16]) {

  print "test16: start 3-process rsm, partition primary, heal it\n";

  start_nodes(3, "ls");

  print "Start lock_tester $p[0]\n";
  $t = spawn("./lock_tester", $p[0]);

  sleep 1;

  print "Partition primary (PID: $pid[0]) on port $p[0] at breakpoint\n";

  spawn("./rsm_tester", $p[0]+1, "partition", 0);

  sleep 3;

  print "first view change wait\n";
  my @lastv = ($p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }

  sleep 1;

  print "Heal partition primary (PID: $pid[0]) on port $p[0] at breakpoint\n";
  spawn("./rsm_tester", $p[0]+1, "partition", 1);

  sleep 1;

  # xxx it should test that this is the 5th view!
  print "second view change wait\n";
  @lastv = ($p[0], $p[1],$p[2]);
  foreach my $port (@lastv) {
    wait_for_view_change(paxos_log($port), $in_views{$port}+1, $port, 20);
  }
  
  print "   Wait for lock_tester to finish (waitpid $t)\n";
  waitpid_to($t, 600);

  if( system( "grep \"passed all tests successfully\" lock_tester-$p[0].log" ) ) {
    mydie( "Failed lock tester for test 16" );
  }

  cleanup();
  sleep 2;
}

print "tests done OK\n";

unlink("config");
