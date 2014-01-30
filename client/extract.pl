#!/usr/bin/perl
use MIME::Base32 qw( RFC );
my $totalResult = "";
foreach $line ( <STDIN> ) {
    #chomp( $line );
    #print "$line\n";
    my @matches = ( $line =~ /text = "(.*?)([^\\])"/g );
    foreach $match(@matches) {
        #$match =~ s/\\(.)/${1}/g;
        #print(eval "$match");
        print($match);
    }
}
