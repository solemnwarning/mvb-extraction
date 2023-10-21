#!/usr/bin/perl
# This script takes the tree produced by ocr.pl, the page text dumps from
# MMVRipper and a flat .cnt file produced by running helpdeco on a .mvb file
# and attempts to reproduce the original hierarchy in a new .cnt file.
#
# By Daniel Collins (2023)
# Released to public domain.

use strict;
use warnings;

use File::Slurp qw(read_file write_file);

if((scalar @ARGV) != 4)
{
	die "Usage: $0 <titles-from-ocr.lst> <txt directory> <flat-cnt-from-helpdeco.cnt> <output.cnt>\n";
}

my ($title_tree, $txt_directory, $flat_cnt, $output_cnt) = @ARGV;

# We do some basic normalisation of titles read in from both files to improve
# matching due to OCR errors, hopefully without losing any important context.
sub normalise_title
{
	my ($title) = @_;
	
	# Remove leading or trailing whitespace
	$title =~ s/^\s*//;
	$title =~ s/\s*$//;
	
	# Collapse repeated groups of whitespace and/or periods..
	$title =~ s/[\s\.]+/ /g;
	
	# Make it all lowercase for good measure
	$title = lc($title);
	
	return $title;
}

sub merge_similar_characters
{
	my ($string) = @_;
	
	$string =~ s/[1LI]/i/gi;
	$string =~ s/[O0]/o/gi;
	$string =~ s/[QG]/q/gi;
	
	return $string;
}

# Read in the tree produced by ocr.pl, $base_depth is the depth of the first
# node and @tree will be populated with elements of the following form:
#
# {
#   depth       => 0,
#   internal_id => 1234,
#   ocr_title   => "Hello  World ",
#   norm_title  => "hello world",
# }

my $base_depth = undef;
my @tree = ();

foreach my $line(read_file($title_tree))
{
	my ($indent, $internal_id, $title) = ($line =~ m/^(\s*)(\d+)\s+(\S[^\r\n]+)$/);
	next unless(defined $title);
	
	my $depth = length($indent) / 2;
	
	$base_depth //= $depth;
	$depth -= $base_depth;
	
	push(@tree, {
		depth       => $depth,
		internal_id => $internal_id,
		ocr_title   => $title,
		norm_title  => normalise_title($title),
	});
}

my %txt_topic_ids = ();

{
	opendir(my $d, $txt_directory) or die "$txt_directory: $!";
	
	while(defined(my $name = readdir($d)))
	{
		if($name =~ m/^(\d+)\.txt$/)
		{
			my $internal_id = $1;
			
			my $text = read_file("${txt_directory}/${name}", { binmode => ":raw" });
			
			my @possible_topic_ids = ($text =~ m/^#:(\S+)\r?\n?$/gm);
			my ($last_possible_topic_id) = $possible_topic_ids[-1];
			
			if(defined $last_possible_topic_id)
			{
				$txt_topic_ids{ $internal_id } = $last_possible_topic_id;
			}
		}
	}
	
	closedir($d);
}

# Read in the flat .cnt file produced by helpdeco. The %topics hash is
# populated with the normalised titles pointing to an array of the original
# topic titles and IDs in pairs.

my @cnt_copy = ();
my %topics = ();

foreach my $line(read_file($flat_cnt))
{
	if($line =~ m/^(:[^\r\n]+)$/)
	{
		# Preserve any directives from the input cnt to be copied into the output.
		push(@cnt_copy, $1);
	}
	elsif($line =~ m/^\d+ ([^=]+)=([^=\r\n]+)$/)
	{
		my $title = $1;
		my $id = $2;
		
		my $normalised_title = normalise_title($title);
		
		push(@{ $topics{$normalised_title} //= [] },
			[ $title, $id ]);
	}
}

# Fill in @tree with the missing topic IDs and un-normalised titles from the
# flat .cnt file.
#
# Each call to match_topics() updates any elements in @tree that can
# unambiguously be matched to a SINGLE topic in %topics before removing it from
# the latter, with progressively fuzzier topic title matching logic to handle
# OCR errors.

my $total_cnt_topics = scalar map { @{ $_ } } values(%topics);
my $total_matched_topics = 0;

sub match_topics
{
	my ($title_eq_func) = @_;
	
	my $prev_item = { norm_title => "" };
	my $matches = 0;
	
	foreach my $item(@tree)
	{
		next if(defined $item->{topic_id});
		
		# If a title exists at multiple points in @tree we don't
		# touch it because we can't know which one is which, UNLESS:
		#
		# a) The title exists in @tree exactly twice.
		#
		# b) The two elements follow each other.
		#
		# c) Only one topic in %topics matches the title.
		#
		# In this case, we assume the first node is an empty container
		# and the second node is the first page in the container, so we
		# set the topic ID on the second one and the topic title on
		# both, which seems to mirror how at least some MMV projects in
		# the wild were authored.
		
		my $tree_nodes_with_this_title = scalar grep { $title_eq_func->($_->{norm_title}, $item->{norm_title}) } @tree;
		my $fix_parent_topic_plz = 0;
		
		if($tree_nodes_with_this_title == 2 && $title_eq_func->($prev_item->{norm_title}, $item->{norm_title}))
		{
			$fix_parent_topic_plz = 1;
		}
		elsif($tree_nodes_with_this_title > 1)
		{
			$prev_item = $item;
			next;
		}
		
		my @matched_topics = grep { $title_eq_func->($_, $item->{norm_title}) } keys(%topics);
		if((scalar @matched_topics) != 1 || (scalar @{ $topics{ $matched_topics[0] } }) != 1)
		{
			# We didn't find exactly ONE match in %topics.
			
			$prev_item = $item;
			next;
		}
		
		if($matched_topics[0] ne $item->{norm_title})
		{
			# The (normalised) topics didn't quite match, but the
			# current compare function says they're good enough.
			
			print "Topic fuzzy matched: ", $item->{norm_title}, " => ", $topics{ $matched_topics[0] }->[0]->[0], "\n";
		}
		
		if($fix_parent_topic_plz)
		{
			$prev_item->{title} = $topics{ $matched_topics[0] }->[0]->[0];
		}
		
		$item->{title} = $topics{ $matched_topics[0] }->[0]->[0];
		$item->{topic_id} = $topics{ $matched_topics[0] }->[0]->[1];
		delete $topics{ $matched_topics[0] };
		
		++$matches;
		++$total_matched_topics;
		
		$prev_item = $item;
	}
	
	return $matches;
}

# First we search for any UNIQUE matches between the cnt/lst files, assigning
# them into the tree and removing them from the topics pool to reduce the
# likelyhood of a topic being miss-assigned later as the matching gets fuzzier.

my $matched_topics = match_topics(sub
{
	my ($title_a, $title_b) = @_;
	return $title_a eq $title_b;
});

print "Matched topics: $matched_topics\n";

# Now we go again, assuming any similar-looking characters were in fact errors
# during the OCR.

my $similar_topics = match_topics(sub
{
	my ($title_a, $title_b) = @_;
	
	$title_a = merge_similar_characters($title_a);
	$title_b = merge_similar_characters($title_b);
	
	return $title_a eq $title_b;
});

print "Matched topics (allowing similar-looking characters): $similar_topics\n";

# And again, but removing any non-alphanumeric characters.

my $punct_topics = match_topics(sub
{
	my ($title_a, $title_b) = @_;
	
	$title_a = merge_similar_characters($title_a);
	$title_b = merge_similar_characters($title_b);
	
	$title_a =~ s/[^0-9A-Z]//gi;
	$title_b =~ s/[^0-9A-Z]//gi;
	
	return $title_a eq $title_b;
});

print "Matched topics (allowing whitespace/punctuation mismatches): $punct_topics\n";

# And again, but searching for topics which match up to the end of the OCR'd
# text, to handle topic titles which didn't fully fit on the index screen.

my $truncated_topics = match_topics(sub
{
	my ($title_a, $title_b) = @_;
	
	$title_b =~ s/.$//;
	
	$title_a = merge_similar_characters($title_a);
	$title_b = merge_similar_characters($title_b);
	
	$title_a =~ s/[^0-9A-Z]//gi;
	$title_b =~ s/[^0-9A-Z]//gi;
	
	return !!($title_a =~ m/^\Q$title_b\E/);
});

print "Matched topics (truncated): $truncated_topics\n";

# See if there are any discrepencies between the topic IDs we merged from the
# .cnt file and the topic IDs we scraped from the page text dumps.

foreach my $item(@tree)
{
	if(defined($item->{topic_id})
		&& defined($txt_topic_ids{ $item->{internal_id} })
		&& lc($item->{topic_id}) ne lc($txt_topic_ids{ $item->{internal_id} }))
	{
		#use Data::Dumper;
		#die "Topic ID mismatch detected!\n".Dumper($item, $txt_topic_ids{ $item->{internal_id} });
		warn "Topic ID mismatch detected";
	}
}

print "Total matched topics from .cnt file: $total_matched_topics / $total_cnt_topics\n";

# Add any topic IDs extracted from the text dumps and titles from the OCR for
# items that haven't already been filled in by processing the .cnt file.

foreach my $item(@tree)
{
	if(!defined($item->{topic_id}) && defined($txt_topic_ids{ $item->{internal_id} }))
	{
		my $txt_topic_id = $txt_topic_ids{ $item->{internal_id} };
		
		foreach my $topics_arr(values %topics)
		{
			my ($cnt_topic) = grep { lc($txt_topic_id) eq lc($_->[1]) } @$topics_arr;
			
			if(defined $cnt_topic)
			{
				$item->{title} //= $cnt_topic->[0];
				$item->{topic_id} //= $cnt_topic->[1];
				
				@$topics_arr = grep { lc($txt_topic_id) ne lc($_->[1]) } @$topics_arr;
				++$total_matched_topics;
			}
		}
		
		$item->{topic_id} //= $txt_topic_id;
	}
	
	$item->{title} //= $item->{ocr_title};
}

# Write out the new .cnt file.

my @cnt_lines = @cnt_copy;

foreach my $item(@tree)
{
	if(defined $item->{topic_id})
	{
		push(@cnt_lines, ($item->{depth} + 1)." ".$item->{title}."=".$item->{topic_id});
	}
	else{
		push(@cnt_lines, ($item->{depth} + 1)." ".$item->{title});
	}
}

write_file($output_cnt, { binmode => ":raw" }, join("\r\n", @cnt_lines, ""));

if($total_matched_topics != $total_cnt_topics)
{
	# Write out a .rej file with the remaining topics from the .cnt file.
	
	my @rej_lines = map { $_->[0]."=".$_->[1] } map { @{ $_ } } values(%topics);
	write_file("${flat_cnt}.rej", { binmode => ":raw" }, join("\r\n", @rej_lines, ""));
	
	print "", (scalar @rej_lines), " unmatched topics written to ${flat_cnt}.rej\n";
}
