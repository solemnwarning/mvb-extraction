#!/usr/bin/perl
# This script performs OCR on the outputs of the MMVIndexRipper program to
# produce a hierarchical listing of titles within a Microsoft Multimedia Viewer
# index.
#
# The tree listing written by MMVIndexRipper should be piped to STDIN and the
# new listing will be written to STDOUT. The bitmaps produced by MMVIndexRipper
# should be in the current working directory.
#
# You will need ImageMagick's convert program and Tesseract OCR installed.
#
# By Daniel Collins (2023)
# Released to public domain.

use strict;
use warnings;

use File::Slurp qw(read_file);
use File::Temp;
use IPC::Run qw(run);

my $tmpdir = File::Temp->newdir();

while(<STDIN>)
{
	my ($indent, $id) = (m/^(\s*)(\d+)/);
	next unless(defined $id);
	
	my $depth = length($indent) / 2;
	
	# The number of pixels to crop from the left edge of the image to
	# exclude the bitmap/lines from the left edge from the OCR process.
	#
	# This is based on the font/DPI used when I ran the MMVIndexRipper and
	# may need adjusting if yours is different.
	#
	# 0: 19px
	# 1: 35px +16
	# 2: 51px +16
	# 3: 68px +17
	# 4: 83px +15
	
	my $crop = 20 + ($depth * 16);
	
	run([ "convert",
		"-alpha" => "deactivate",
		"-negate",
		"-colorspace" => "gray",
		"-fill" => "white",
		"-crop" => "+$crop",
		"$id.bmp", "${tmpdir}/tmp.png" ]) or die;
	
	my $tesseract_out = "";
	run([ "tesseract",
		"--dpi" => "240",
		"-l" => "eng",
		"--psm" => 7, # Treat the image as a single text line.
		"${tmpdir}/tmp.png", "${tmpdir}/tmp" ],
		">&" => \$tesseract_out) or die $tesseract_out;
	
	my ($text) = read_file("${tmpdir}/tmp.txt");
	chomp($text);
	
	$text =~ s/\|$//;
	
	print "${indent}${id}  ${text}\n";
}
