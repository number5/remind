package Remind::PDF;
# SPDX-License-Identifier: GPL-2.0-only
use strict;
use warnings;

use Cairo;
use Pango;

use Remind::PDF::Entry;
use Encode;

use JSON::MaybeXS;

=head1 NAME

Remind::PDF - Render a month's worth of Remind data to PDF

=head1 CLASS METHODS

=head2 Remind::PDF->create_from_stream($in, $specials_accepted)

This method reads data from an open file handle C<$in>.  C<$specials_accepted>
is a hashref of SPECIAL reminder types to accept; the key is the name of the
SPECIAL (all lower-case) and the value should be 1.  Any SPECIAL reminders
not in the hash are ignored.

This function returns a two-element array: C<($obj, $err)>.  On success,
C<$obj> will be a C<Remind::PDF> object and C<$err> will be undef.  On
failure, C<$obj> will be undef and C<$err> will be an error message.

=cut
sub create_from_stream
{
        my ($class, $in, $specials_accepted) = @_;
        while (<$in>) {
                chomp;
                if ($_ eq '# rem2ps begin'  ||
                    $_ eq '# rem2ps2 begin') {
                        my $self = bless {}, $class;
                        return $self->read_one_month($in, $_, $specials_accepted);
                } elsif ($_ eq '[') {
                        return Remind::PDF::Multi->create_from_stream($in, $specials_accepted);
                }
        }
        return (undef, "Could not find any remind -p output anywhere");
}

=head2 Remind::PDF->create_from_hash($hash, $specials_accepted)

This method takes data from a hash C<$hash>, which must be one month's
worth of data from C<remind -ppp> output.  C<$specials_accepted> is a
hashref of SPECIAL reminder types to accept; the key is the name of
the SPECIAL (all lower-case) and the value should be 1.  Any SPECIAL
reminders not in the hash are ignored.

This function returns a two-element array: C<($obj, $err)>.  On success,
C<$obj> will be a C<Remind::PDF> object and C<$err> will be undef.  On
failure, C<$obj> will be undef and C<$err> will be an error message.

=cut
sub create_from_hash
{
        my ($class, $hash, $specials_accepted) = @_;

        if (exists($hash->{caltype}) && ($hash->{caltype} eq 'weekly')) {
                return Remind::PDF::Weekly->create_from_hash($hash, $specials_accepted);
        }
        bless $hash, $class;

        my $filtered_entries = [];
        for (my $i=0; $i<=31; $i++) {
                $filtered_entries->[$i] = [];
        }

        foreach my $e (@{$hash->{entries}}) {
                if ($hash->accept_special($e, $specials_accepted)) {
                        my $day = $e->{date};
                        $day =~ s/^\d\d\d\d-\d\d-//;
                        $day =~ s/^0//;
                        push(@{$filtered_entries->[$day]}, Remind::PDF::Entry->new_from_hash($e));
                }
        }
        $hash->{entries} = $filtered_entries;
        return $hash;
}

=head1 INSTANCE METHODS

=head2 read_one_month($in, $first_line, $specials_accepted)

This function reads one month's worth of data from the file handle
C<$in>.  C<$first_line> is the line that was read from C<$in>
just before calling this function.  C<$specials_accepted> is a
hashref as documented above.

The return value is the same C<($obj, $err)> two-element array
as C<create_from_stream> returns.

=cut
sub read_one_month
{
        my ($self, $in, $first_line, $specials_accepted) = @_;
        $self->{entries} = [];
        $self->{daynames} = [];
        $self->{monthname} = '';
        $self->{year} = '';
        $self->{daysinmonth} = 0;
        $self->{firstwkday} = 0;
        $self->{mondayfirst} = 0;
        $self->{prevmonthname} = '';
        $self->{nextmonthname} = '';
        $self->{daysinprevmonth} = 0;
        $self->{daysinnextmonth} = 0;
        $self->{prevmonthyear} = 0;
        $self->{nextmonthyear} = 0;
        for (my $i=0; $i<=31; $i++) {
                $self->{entries}->[$i] = [];
        }

        my $line = $in->getline();
        chomp($line);

        # Month Year Days FirstWkday MondayFirst
        if ($line =~ /^(\S+) (\d+) (\d+) (\d+) (\d+)/) {
                $self->{monthname} = $1;
                $self->{year} = $2;
                $self->{daysinmonth} = $3;
                $self->{firstwkday} = $4;
                $self->{mondayfirst} = $5;
        } else {
                return (undef, "Cannot interpret line: $line");
        }

        $self->{monthname} =~ s/_/ /g;
        # Day names
        $line = $in->getline();
        chomp($line);
        if ($line =~ /^\S+ \S+ \S+ \S+ \S+ \S+ \S+$/) {
                @{$self->{daynames}} = map { s/_/ /g; $_; } (split(/ /, $line)); ## no critic
        } else {
                return (undef, "Cannot interpret line: $line");
        }

        # Prev month, num days
        $line = $in->getline();
        chomp($line);
        if ($line =~ /^\S+ \d+$/) {
                ($self->{prevmonthname}, $self->{daysinprevmonth}) = split(/ /, $line);
        } else {
                return (undef, "Cannot interpret line: $line");
        }
        # Next month, num days
        $line = $in->getline();
        chomp($line);
        if ($line =~ /^\S+ \d+$/) {
                ($self->{nextmonthname}, $self->{daysinnextmonth}) = split(/ /, $line);
        } else {
                return (undef, "Cannot interpret line: $line");
        }

        $self->{prevmonthname} =~ s/_/ /g;
        $self->{nextmonthname} =~ s/_/ /g;

        if ($first_line eq '# rem2ps2 begin') {
                # remind -pp format
                return $self->read_one_month_pp($in, $specials_accepted);
        }

        # Old-style "remind -p"
        # TODO: Eventually support this?
        return $self->read_one_month_p($in, $specials_accepted);
}

=head2 read_one_month_p($in, $specials_accepted)

This function reads one month's worth of data from the file handle
C<$in>, assuming the original "remind -p" format.
C<$specials_accepted> is a hashref as documented above.

The return value is the same C<($obj, $err)> two-element array
as C<create_from_stream> returns.

=cut
sub read_one_month_p
{
        my ($self, $in, $specials_accepted) = @_;
        my $line;
        while ($line = $in->getline()) {
                chomp($line);
                if ($line eq '# rem2ps end') {
                        return ($self, undef);
                }
                # Ignore comments
                next if $line =~ /^#/;
                my $hash = $self->parse_oldstyle_line($line);
                next unless $hash;

                my $day = $hash->{date};
                $day =~ s/^\d\d\d\d-\d\d-//;
                $day =~ s/^0//;
                if ($self->accept_special($hash, $specials_accepted)) {
                        push(@{$self->{entries}->[$day]}, Remind::PDF::Entry->new_from_hash($hash));
                }
        }
        return (undef, "Missing # rem2ps end marker");
}

=head2 parse_oldstyle_line ($line)

This method parses an old-style "remind -p" line
and returns a hashref containing some or all of the
hash keys found in the newer "remind -pp" JSON output.

=cut
sub parse_oldstyle_line
{
        my ($self, $line) = @_;
        return unless $line =~ m|^(\d+)/(\d+)/(\d+) (\S+) (\S+) (\S+) (\S+) (.*)$|;

        my $hash = {
                date => "$1-$2-$3",
                passthru => $4,
                tags => $5,
                duration => $6,
                time => $7,
                body => $8};
        foreach my $key (qw(passthru tags time duration)) {
                delete $hash->{$key} if $hash->{$key} eq '*';
        }

        if ($hash->{passthru}) {
                if ($hash->{passthru} =~ /^(shade|color|colour)$/i) {
                        if ($hash->{body} =~ /^\s*(\d+)\s+(\d+)\s+(\d+)\s*(.*)/) {
                                $hash->{r} = $1;
                                $hash->{g} = $2;
                                $hash->{b} = $3;
                                $hash->{body} = $4;
                        } elsif ($hash->{body} =~ /^\s*(\d+)\s*(.*)/) {
                                $hash->{r} = $1;
                                $hash->{g} = $1;
                                $hash->{b} = $1;
                                $hash->{body} = $2;
                        }
                }
        }
        return $hash;
}

=head2 setup_daymap

Set up the array that maps ($row, $col) to day number (or -1
for rows/cols out of range.)

=cut
sub setup_daymap
{
        my ($self, $settings) = @_;
        # First column
        my $first_col = $self->{firstwkday};
        if ($self->{mondayfirst}) {
                $first_col--;
                if ($first_col < 0) {
                        $first_col = 6;
                }
        }

        # Last column
        my $last_col = ($first_col + $self->{daysinmonth} - 1) % 7;

        # Number of rows
        my $rows = 1;
        my $last_day_on_row = 7 - $first_col;
        while ($last_day_on_row < $self->{daysinmonth}) {
                $last_day_on_row += 7;
                $rows++;
        }

        # Add a row for small calendars if necessary
        if (($settings->{small_calendars} != 0) && ($first_col == 0) && ($last_col == 6)) {
                $rows++;
                $self->{extra_row} = 1;
        } else {
                $self->{extra_row} = 0;
        }
        $self->{rows} = $rows;
        $self->{daymap} = [];
        $self->{first_col} = $first_col;
        $self->{last_col} = $last_col;
        for (my $row=0; $row<$rows; $row++) {
                for (my $col=0; $col < 7; $col++) {
                        $self->{daymap}->[$row]->[$col] = -1;
                }
        }
        $self->{nextcal_row} = -1;
        $self->{prevcal_row} = -1;
        $self->{nextcal_col} = 6;
        $self->{prevcal_col} = 0;
        # Figure out where to draw the small calendars
        my $extra_row = $self->{extra_row};
        if ($settings->{small_calendars} == 1) {
                if ($last_col <= 4 || ($last_col == 6 && $extra_row)) {
                        $self->{prevcal_row} = $rows-1;
                        $self->{prevcal_col} = 5;
                        $self->{nextcal_row} = $rows-1;
                        $self->{nextcal_col} = 6;
                } else {
                        $self->{prevcal_row} = 0;
                        $self->{prevcal_col} = 0;
                        $self->{nextcal_row} = 0;
                        $self->{nextcal_col} = 1;
                }
        } elsif ($settings->{small_calendars} == 2) {
                if ($first_col >= 2) {
                        $self->{prevcal_row} = 0;
                        $self->{prevcal_col} = 0;
                        $self->{nextcal_row} = 0;
                        $self->{nextcal_col} = 1;
                } else {
                        $self->{prevcal_row} = $rows-1;
                        $self->{prevcal_col} = 5;
                        $self->{nextcal_row} = $rows-1;
                        $self->{nextcal_col} = 6;
                }
        } elsif ($settings->{small_calendars} == 3) {
                if ($first_col >= 1 && $last_col <= 5) {
                        $self->{prevcal_row} = 0;
                        $self->{prevcal_col} = 0;
                        $self->{nextcal_row} = $rows-1;
                        $self->{nextcal_col} = 6;
                } else {
                        if ($last_col <= 4 || ($last_col == 6 && $extra_row)) {
                                $self->{prevcal_row} = $rows-1;
                                $self->{prevcal_col} = 5;
                                $self->{nextcal_row} = $rows-1;
                                $self->{nextcal_col} = 6;
                        } else {
                                $self->{prevcal_row} = 0;
                                $self->{prevcal_col} = 0;
                                $self->{nextcal_row} = 0;
                                $self->{nextcal_col} = 1;
                        }
                }
        }

        my $col = $first_col;
        my $row = 0;
        my $day = 1;
        while ($day <= $self->{daysinmonth}) {
                $self->{daymap}->[$row]->[$col] = $day;
                $day++;
                $col++;
                if ($col > 6) {
                        $row++;
                        $col = 0;
                }
        }

        # Check if we should wrap the calendar
        if ($self->{rows} == 6 && $settings->{wrap_calendar}) {
                # Move everything in the last row to the first row
                my $occupied_col = 0;
                for (my $col=0; $col<7; $col++) {
                        if ($self->{daymap}->[5]->[$col] > 0) {
                                $self->{daymap}->[0]->[$col] = $self->{daymap}->[5]->[$col];
                                $occupied_col = $col;
                        } else {
                                last;
                        }
                }
                if ($settings->{small_calendars}) {
                        $self->{prevcal_row} = 0;
                        $self->{prevcal_col} = $occupied_col+1;
                        $self->{nextcal_row} = 0;
                        $self->{nextcal_col} = $occupied_col+2;
                        for (my $col = 6; $col > 0; $col--) {
                                if ($self->{daymap}->[0]->[$col] < 0) {
                                        $self->{nextcal_col} = $col;
                                        last;
                                }
                        }
                }
                $self->{rows} = 5;
        }
}

=head2 read_one_month_pp($in, $specials_accepted)

This function reads one month's worth of data from the file handle
C<$in>, assuming the "remind -pp" partial-JSON format.
C<$specials_accepted> is a hashref as documented above.

The return value is the same C<($obj, $err)> two-element array
as C<create_from_stream> returns.

=cut
sub read_one_month_pp
{
        my ($self, $in, $specials_accepted) = @_;

        my $json = JSON::MaybeXS->new(utf8 => 0);
        my $line;
        while ($line = $in->getline()) {
                chomp($line);
                if ($line eq '# rem2ps2 end') {
                        return ($self, undef);
                }
                my $hash;
                eval {
                        $hash = $json->decode($line);
                };
                if (!$hash) {
                        return (undef, "Unable to decode JSON: $@");
                }

                my $day = $hash->{date};
                $day =~ s/^\d\d\d\d-\d\d-//;
                $day =~ s/^0//;
                if ($self->accept_special($hash, $specials_accepted)) {
                        push(@{$self->{entries}->[$day]}, Remind::PDF::Entry->new_from_hash($hash));
                }
        }
        return (undef, "Missing # rem2ps2 end marker");
}

=head2 accept_special($hash, $specials_accepted)

Given a hashref C<$hash> consisting of one entry parsed
from the "remind -p" stream and a C<$specials_accepted> hash,
return 1 if we should include this entry in the calendar or
0 if not.

=cut
sub accept_special
{
        my ($self, $hash, $specials_accepted) = @_;
        return 1 unless exists($hash->{passthru});
        return 1 if $specials_accepted->{lc($hash->{passthru})};
        return 0;
}

=head2 find_last_special($special, $entries)

Given an array of Reminder entries, find the last
C<$special>-type SPECIAL in the array.  Return
the entry if one was found or undef if not.

=cut
sub find_last_special
{
        my ($self, $special, $entries) = @_;
        my $class = "Remind::PDF::Entry::$special";
        my $found = undef;
        foreach my $e (@$entries) {
                $found = $e if ($e->isa($class));
        }
        return $found;
}

=head2 render($cr, $settings)

Render a calendar for one month.  C<$cr> is a Cairo
drawing context, and C<$settings> is a settings hash
passed in by the caller.  See the source code of
C<rem2pdf> for the contents of C<$settings>

=cut
sub render
{
        my ($self, $cr, $settings, $index, $total) = @_;

        if ($settings->{daynum_size} < 0) {
                $settings->{daynum_size} = 14;
        }
        if ($settings->{svg} || $settings->{eps}) {
                if ($index > 1) {
                        if ($index == 2) {
                                print STDERR "WARNING: --svg/--eps can only output one page; ignoring subsequent\nmonths in a multi-month calendar.\n";
                        }
                        return;
                }
        }
        my ($r, $g, $b) = @{$settings->{bg_color}};
        if ($r >= 0 && $g >= 0 && $b >= 0) {
                $cr->save;
                $self->set_cr_color($cr, $settings->{bg_color});
                $cr->rectangle(0, 0, $settings->{width}, $settings->{height});
                $cr->fill();
                $cr->restore;
        }
        $self->setup_daymap($settings);
        $self->{horiz_lines} = [];
        $cr->set_line_cap('square');
        my $so_far = $self->draw_title($cr, $settings);

        # Top line
        push(@{$self->{horiz_lines}}, $so_far);

        my $top_line = $so_far;

        $so_far = $self->draw_daynames($cr, $settings, $so_far);

        # Line under the days
        push(@{$self->{horiz_lines}}, $so_far);

        # Remaining space on page
        $self->{remaining_space} = $settings->{height} - $settings->{margin_bottom} - $so_far;

        $self->{minimum_row_height} = $self->{remaining_space} / 9;

        # Row height if we are filling the page
        $self->{row_height} = $self->{remaining_space} / $self->{rows};

        for (my $row = 0; $row < $self->{rows}; $row++) {
                my $old_so_far = $so_far;
                $so_far = $self->draw_row($cr, $settings, $so_far, $row);
                push(@{$self->{horiz_lines}}, $so_far);
                if ($row == $self->{prevcal_row}) {
                        my ($x1, $y1, $x2, $y2) = $self->col_box_coordinates($old_so_far, $self->{prevcal_col}, $so_far - $old_so_far, $settings);
                        $self->draw_small_calendar($cr, $x1 + $settings->{border_size}, $y1 + $settings->{border_size},
                                                   $x2 - $x1 - 2*$settings->{border_size}, $y2 - $y1 - 2*$settings->{border_size},
                                                   $settings, $self->{prevmonthname}, $self->{daysinprevmonth}, ($self->{first_col} + 35 - $self->{daysinprevmonth}) % 7);
                }
                if ($row == $self->{nextcal_row}) {
                        my ($x1, $y1, $x2, $y2) = $self->col_box_coordinates($old_so_far, $self->{nextcal_col}, $so_far - $old_so_far, $settings);
                        $self->draw_small_calendar($cr, $x1 + $settings->{border_size}, $y1 + $settings->{border_size},
                                                   $x2 - $x1 - 2*$settings->{border_size}, $y2 - $y1 - 2*$settings->{border_size},
                                                   $settings, $self->{nextmonthname}, $self->{daysinnextmonth}, ($self->{last_col} + 1) % 7);
                }
        }

        if ($so_far > $settings->{height} - $settings->{margin_bottom} + 1) {
                print STDERR "WARNING: overfull calendar box\n";
        }
        # The vertical lines

        # Set the color
        $cr->save;
        $self->set_cr_color($cr, $settings->{line_color});
        my $cell = ($settings->{width} - $settings->{margin_left} - $settings->{margin_right}) / 7;
        for (my $i=0; $i<=7; $i++) {
                $cr->move_to($settings->{margin_left} + $i * $cell, $top_line);
                $cr->line_to($settings->{margin_left} + $i * $cell, $so_far);
                $cr->stroke();
        }

        # And the horizontal lines
        foreach my $y (@{$self->{horiz_lines}}) {
                $cr->move_to($settings->{margin_left}, $y);
                $cr->line_to($settings->{width} - $settings->{margin_right}, $y);
                $cr->stroke();
        }

        $cr->restore;
        if ($settings->{verbose}) {
                print STDERR "rem2pdf: Rendered " . $self->{monthname} . ' ' . $self->{year} . "\n";
        }
        # Done this page
        $cr->show_page();
}

sub set_cr_color {
        my ($self, $cr, $color_array) = @_;
        my ($r, $g, $b) = @$color_array;
        if ($r < 0 || $g < 0 || $b < 0 ||
            $r > 255 || $g > 255 || $b > 255) {
                return;
        }
        $cr->set_source_rgb($r / 255,
                            $g / 255,
                            $b / 255);
}

=head2 draw_row($cr, $settings, $so_far, $row, $start_day, $start_col)

Draw a single row in the calendar.  C<$cr> is a Cairo drawing context
and C<$settings> is the settings hash passed to C<render>.  C<$so_far>
is the Y-coordinate of the top of the row; drawing starts at this
coordinate.  C<$start_day> is the day of the month at which the
row starts and C<$start> col is the column number (0-6) at which
to start drawing from C<$start_day>

Returns the Y coordinate at which to start drawing the I<next>
calendar row.

=cut
sub draw_row
{
        my ($self, $cr, $settings, $so_far, $row) = @_;

        my $height = 0;

        # Preview them to figure out the row height...
        if (!$settings->{fill_entire_page}) {
                for (my $col=0; $col<7; $col++) {
                        my $day = $self->{daymap}->[$row]->[$col];
                        next if ($day < 1);
                        my $h = $self->draw_day($cr, $settings, $so_far, $day, $col, 0);
                        $height = $h if ($h > $height);
                }
        } else {
                $height = $self->{row_height} - $settings->{border_size} * 2;
        }

        if (!$settings->{fill_entire_page} && $height < $self->{minimum_row_height}) {
                $height = $self->{minimum_row_height};
        }
        # Now draw for real
        for (my $col=0; $col<7; $col++) {
                my $day = $self->{daymap}->[$row]->[$col];
                next if ($day < 1);
                $self->draw_day($cr, $settings, $so_far, $day, $col, $height);
        }

        return $so_far + $height + $settings->{border_size} * 2;
}

=head2 col_box_coordinates($so_far, $col, $height, $settings)

Returns a four-element array C<($x1, $y1, $x2, $y2)> representing
the bounding box of the calendar box at column C<$col> (0-6).  C<$height>
is the height of the box and C<$settings> is the settings hashref
passed to C<render>.

=cut
sub col_box_coordinates
{
        my ($self, $so_far, $col, $height, $settings) = @_;
        my $cell = ($settings->{width} - $settings->{margin_left} - $settings->{margin_right}) / 7;

        return (
                $settings->{margin_left} + $cell * $col,
                $so_far,
                $settings->{margin_left} + $cell * ($col + 1),
                $so_far + $height + 2* $settings->{border_size},
            );
}

=head2 draw_day($cr, $settings, $so_far, $day, $col, $height)

Renders a single day's worth of reminders.  C<$cr> is a Cairo
drawing context and C<$settings> is the settings hash passed
to C<render>.  C<$so_far> is the Y-coordinate of the top
of the box and C<$col> is the column number.

C<$height> is the height of the box.  If C<$height> is passed
in as zero, then do not actually render anything... instead,
compute how high the box should be.  If C<$height> is non-zero,
then it is the height of the box.

Returns the height required for the calendar box.

=cut
sub draw_day
{
        my ($self, $cr, $settings, $so_far, $day, $col, $height) = @_;

        my $top = $so_far;

        my ($x1, $y1, $x2, $y2) = $self->col_box_coordinates($so_far, $col, $height, $settings);

        # Do shading if we're in "for real" mode
        if ($height) {
                my $shade = $self->find_last_special('shade', $self->{entries}->[$day]);
                if ($shade) {
                        $cr->save;
                        $cr->set_source_rgb($shade->{r} / 255,
                                            $shade->{g} / 255,
                                            $shade->{b} / 255);
                        $cr->rectangle($x1, $y1, $x2 - $x1, $y2 - $y1);
                        $cr->fill();
                        $cr->restore;
                }
        }
        # Draw the day number
        my $layout = Pango::Cairo::create_layout($cr);
        $layout->set_text($day);
        my $desc = Pango::FontDescription->from_string($settings->{daynum_font} . ' ' . $settings->{daynum_size} . 'px');

        $layout->set_font_description($desc);
        my ($wid, $h) = $layout->get_pixel_size();


        # Don't actually draw if we're just previewing to get the cell height
        if ($height) {
                $cr->save;
                $self->set_cr_color($cr, $settings->{daynum_color});
                if ($settings->{numbers_on_left}) {
                        $cr->move_to($x1 + $settings->{border_size}, $so_far + $settings->{border_size});
                } else {
                        $cr->move_to($x2 - $settings->{border_size} - $wid, $so_far + $settings->{border_size});
                }
                Pango::Cairo::show_layout($cr, $layout);
                $cr->restore();
        }

        $so_far += $h + $settings->{border_size};
        my $entry_height = 0;
        my $done = 0;
        foreach my $entry (@{$self->{entries}->[$day]}) {
                # Moon and week should not adjust height
                if ($entry->isa('Remind::PDF::Entry::moon') ||
                    $entry->isa('Remind::PDF::Entry::week')) {
                        $entry->render($self, $cr, $settings, $top, $day, $col, $height);
                        next;
                }

                # An absolutely-positioned Pango markup should not adjust height
                # either
                if ($entry->isa('Remind::PDF::Entry::pango') &&
                    defined($entry->{atx}) && defined($entry->{aty})) {
                        $entry->render($self, $cr, $settings, $top, $day, $col, $height);
                        next;
                }

                # Shade is done already
                if ($entry->isa('Remind::PDF::Entry::shade')) {
                        next;
                }
                if ($done) {
                        $so_far += $settings->{border_size};
                        $entry_height += $settings->{border_size};
                }
                $done = 1;
                my $h2 = $entry->render($self, $cr, $settings, $so_far, $day, $col, $height);
                $entry_height += $h2;
                $so_far += $h2;
        }
        if ($height) {
                if ($h + $entry_height + 2 * $settings->{border_size} > $height) {
                        print STDERR "WARNING: overfull box at $day " . $self->{monthname} . ' ' . $self->{year} . "\n";
                        $entry_height = $height;
                }
        }
        return $h + $entry_height + 2 * $settings->{border_size};
}

=head2 draw_daynames($cr, $settings, $so_far)

Draw the weekday names heading.  C<$cr> is a Cairo drawing context
and C<$settings> is the settings hash passed to C<render>.  C<$so_far>
is the Y-coordinate of the top of the row; drawing starts at this
coordinate.

Returns the Y coordinate at which to start drawing the first
calendar row.

=cut
sub draw_daynames
{
        my ($self, $cr, $settings, $so_far) = @_;

        my $w = $settings->{width} - $settings->{margin_left} - $settings->{margin_right};
        my $cell = $w/7;

        $so_far += $settings->{border_size};
        my $height = 0;
        for (my $i=0; $i<7; $i++) {
                my $j;
                if ($self->{mondayfirst}) {
                        $j = ($i + 1) % 7;
                } else {
                        $j = $i;
                }
                my $layout = Pango::Cairo::create_layout($cr);
                $layout->set_text(Encode::decode('UTF-8', $self->{daynames}->[$j]));
                my $desc = Pango::FontDescription->from_string($settings->{header_font} . ' ' . $settings->{header_size} . 'px');

                $layout->set_font_description($desc);

                my ($wid, $h) = $layout->get_pixel_size();
                $cr->save;
                $self->set_cr_color($cr, $settings->{header_color});
                $cr->move_to($settings->{margin_left} + $i * $cell + $cell/2 - $wid/2, $so_far);
                Pango::Cairo::show_layout($cr, $layout);
                $cr->restore();
                if ($h > $height) {
                        $height = $h;
                }
        }
        return $so_far + $height + $settings->{border_size} * .75;
}

=head2 draw_title($cr, $settings)

Draw the title ("Monthname Year") at the top of the calendar.
C<$cr> is a Cairo drawing context
and C<$settings> is the settings hash passed to C<render>.

Returns the Y coordinate at which to start drawing the row
containing the weekday names.

=cut
sub draw_title
{
        my ($self, $cr, $settings) = @_;
        my $title = $self->{monthname} . ' ' . $self->{year};

        my $url = $settings->{title_url} // '';

        # set_page_label not available in older versions of Cairo
        eval { $cr->get_target()->set_page_label($title); };
        my $layout = Pango::Cairo::create_layout($cr);
        $layout->set_text(Encode::decode('UTF-8', $title));
        my $desc = Pango::FontDescription->from_string($settings->{title_font} . ' ' . $settings->{title_size} . 'px');

        $layout->set_font_description($desc);

        my ($w, $h) = $layout->get_pixel_size();
        $cr->save();
        $self->set_cr_color($cr, $settings->{title_color});
        $cr->move_to($settings->{width}/2 - $w/2, $settings->{margin_top});
        if ($url ne '') {
                $cr->tag_begin(Cairo::TAG_LINK, "uri='$url'");
        }
        Pango::Cairo::show_layout($cr, $layout);
        if ($url ne '') {
                $cr->tag_end(Cairo::TAG_LINK);
        }
        $cr->restore();
        return $h + $settings->{margin_top} + $settings->{border_size};
}

=head2 get_rgb($color)

Parses a 3- or 6-hex-digit color value and returns [red, green, blue].  If
color could not be parsed, returns undef

=cut
sub get_rgb
{
        my ($self, $color) = @_;
        my ($r, $g, $b);
        if ($color =~ /^([0-9a-f])([0-9a-f])([0-9a-f])$/i) {
                $r = hex($1);
                $g = hex($2);
                $b = hex($3);
                $r = $r * 16 + $r;
                $g = $g * 16 + $g;
                $b = $b * 16 + $b;
                return [$r, $g, $b];
        } elsif ($color =~ /^([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i) {
                return [hex($1), hex($2), hex($3)];
        }
        return undef;
}


=head2 draw_small_calendar($cr, $x, $y, $width, $height, $settings, $month, $days, $start_wkday)

Draw a small calendar on the Cairo context C<$cr>.  The top left-hand
corner of the box is at C<($x, $y)> and the size of the box is
C<($width, $height>).  $settings is the settings hashref passed to
C<render>.  C<$month> is the name of the month to draw and C<$days> is
the number of days in the month.  Finally, C<$start_wkday> is the
weekday (0=Sunday, 6=Saturday) on which the month starts

=cut
sub draw_small_calendar
{
        my ($self, $cr, $x, $y, $width, $height, $settings, $month, $days, $start_wkday) = @_;

        my $first_col = $start_wkday;
        if ($self->{mondayfirst}) {
                $first_col--;
                if ($first_col < 0) {
                        $first_col = 6;
                }
        }

        # Last column
        my $last_col = ($first_col + $days - 1) % 7;

        # Number of rows
        my $rows = 1;
        my $last_day_on_row = 7 - $first_col;
        while ($last_day_on_row < $days) {
                $last_day_on_row += 7;
                $rows++;
        }
        my $font_size = $self->calculate_small_calendar_font_size($cr, $width, $height, $settings, $rows);

        my $layout = Pango::Cairo::create_layout($cr);
        my $desc = Pango::FontDescription->from_string($settings->{small_cal_font} . ' ' . $font_size . 'px');
        $layout->set_font_description($desc);
        $layout->set_text('88 ');
        my ($wid, $h) = $layout->get_pixel_size();
        $h += 1;
        # Month name
        $layout = Pango::Cairo::create_layout($cr);
        $desc = Pango::FontDescription->from_string($settings->{small_cal_font} . ' ' . $font_size . 'px');
        $layout->set_font_description($desc);
        $layout->set_text(Encode::decode('UTF-8', $month));
        my ($mw, $mh) = $layout->get_pixel_size();
        $cr->save();
        $self->set_cr_color($cr, $settings->{smallcal_color});
        $cr->move_to($x + $width/2 - $mw/2, $y);
        Pango::Cairo::show_layout($cr, $layout);
        $cr->restore();

        $y += $h;
        # Day names
        $wid = $width / 7;
        for (my $col=0; $col <7; $col++) {
                my $j;
                if ($self->{mondayfirst}) {
                        $j = ($col + 1) % 7;
                } else {
                        $j = $col;
                }
                my $day = $self->{daynames}->[$j];
                my $l = substr(Encode::decode('UTF-8', $day), 0, 1);
                $layout = Pango::Cairo::create_layout($cr);
                $desc = Pango::FontDescription->from_string($settings->{small_cal_font} . ' ' . $font_size . 'px');
                $layout->set_font_description($desc);
                $layout->set_text($l);
                $cr->save();
                $self->set_cr_color($cr, $settings->{smallcal_color});
                $cr->move_to($x + $col*$wid, $y);
                Pango::Cairo::show_layout($cr, $layout);
                $cr->restore();
        }
        $y += $h;

        my $col = $start_wkday;

        for (my $d=1; $d <= $days; $d++) {
                $desc = Pango::FontDescription->from_string($settings->{small_cal_font} . ' ' . $font_size . 'px');
                $layout->set_font_description($desc);
                my $dt = $d;
                if (length($dt) < 2) {
                        $dt = ' ' . $dt;
                }
                $layout->set_text($dt);
                $cr->save();
                $self->set_cr_color($cr, $settings->{smallcal_color});
                $cr->move_to($x + $col*$wid, $y);
                Pango::Cairo::show_layout($cr, $layout);
                $cr->restore();
                $col++;
                if ($col > 6) {
                        $col = 0;
                        $y += $h;
                }
        }
}

sub calculate_small_calendar_font_size
{
        my ($self, $cr, $width, $height, $settings, $rows) = @_;

        my $layout = Pango::Cairo::create_layout($cr);
        my $desc = Pango::FontDescription->from_string($settings->{small_cal_font} . ' ' . '10px');
        $layout->set_font_description($desc);
        $layout->set_text('88 88 88 88 88 88 88');
        my ($wid, $h) = $layout->get_pixel_size();
        $h += 1;
        $h *= ($rows + 2); # row for month name; row for day names

        my $scale = $width / $wid;
        if (($height / $h) < $scale) {
                $scale = $height / $h;
        }
        my $font_size = int($scale * 10);

        # Check
        while(1) {
                $desc = Pango::FontDescription->from_string($settings->{small_cal_font} . ' ' . $font_size . 'px');
                $layout->set_font_description($desc);
                $layout->set_text('88 88 88 88 88 88 88');
                ($wid, $h) = $layout->get_pixel_size();
                $h += 1;
                $h *= ($rows + 2); # row for month name; row for day names

                $scale = $width / $wid;
                if (($height / $h) < $scale) {
                        $scale = $height / $h;
                }

                if ($scale >= 1) { # Font size is OK
                        last;
                }
                $font_size -= 0.1;
        }
        return $font_size;
}

package Remind::PDF::Multi;

=head1 NAME

Remind::PDF::Multi - A container for multiple months' worth of calendar data

=head1 DESCRIPTION

The C<remind -ppp> output consists of a JSON array with each element
representing one month's worth of reminders.  C<Remind::PDF::Multi>
reads this output and returns an instance of itself containing
an array of C<Remind::PDF> objects, one object for each month.

=head1 CLASS METHODS

=head2 Remind::PDF::Multi->create_from_stream($in, $specials_accepted)

This method reads data from an open file handle C<$in>.  C<$specials_accepted>
is a hashref of SPECIAL reminder types to accept; the key is the name of the
SPECIAL (all lower-case) and the value should be 1.  Any SPECIAL reminders
not in the hash are ignored.

This function returns a two-element array: C<($obj, $err)>.  On
success, C<$obj> will be a C<Remind::PDF::Multi> object and C<$err>
will be undef.  On failure, C<$obj> will be undef and C<$err> will be
an error message.

=cut


sub create_from_stream
{
        my ($class, $in, $specials_accepted) = @_;
        my $json = "[\n";
        my $right_bracket = 0;
        my $right_curly = 0;

        while(<$in>) {
                $json .= $_;
                chomp;
                if ($_ eq ']') {
                        $right_bracket++;
                        if ($right_bracket == 2 && $right_curly == 1) {
                                return $class->create_from_json($json, $specials_accepted);
                        }
                } elsif($_ eq '}') {
                        $right_curly++;
                } else {
                        $right_bracket = 0;
                        $right_curly = 0;
                }
        }
        return(undef, 'Unable to parse JSON stream');
}

=head2 Remind::PDF::Multi->create_from_json($json, $specials_accepted)

This method takes data from a JSON string <$json>.  C<$specials_accepted>
is a hashref of SPECIAL reminder types to accept; the key is the name of the
SPECIAL (all lower-case) and the value should be 1.  Any SPECIAL reminders
not in the hash are ignored.

This function returns a two-element array: C<($obj, $err)>.  On
success, C<$obj> will be a C<Remind::PDF::Multi> object and C<$err>
will be undef.  On failure, C<$obj> will be undef and C<$err> will be
an error message.

=cut

sub create_from_json
{
        my ($class, $json, $specials_accepted) = @_;
        my $parser = JSON::MaybeXS->new(utf8 => 0);

        my $array;
        eval {
                $array = $parser->decode($json);
        };
        if (!$array) {
                return (undef, "Unable to decode JSON: $@");
        }
        if (ref($array) ne 'ARRAY') {
                return (undef, "Expecting array; found " . ref($array));
        }

        my $self = bless { months => []}, $class;
        foreach my $m (@$array) {
                my ($e, $error) = Remind::PDF->create_from_hash($m, $specials_accepted);
                if (!$e) {
                        return (undef, $error);
                }
                push(@{$self->{entries}}, $e);
        }
        return ($self, undef);
}

=head1 INSTANCE METHODS

=head2 render($cr, $settings)

Iterate through all the C<Remind::PDF> objects
and call their C<render> methods.  This method
renders as many months worth of calendar data
as were read from the C<remind -ppp> stream

=cut
sub render
{
        my ($self, $cr, $settings) = @_;
        my $index = 0;
        my $total = scalar(@{$self->{entries}});

        foreach my $e (@{$self->{entries}}) {
                $index++;
                $e->render($cr, $settings, $index, $total);
        }
}

package Remind::PDF::Weekly;
use base qw(Remind::PDF);

=head1 NAME

Remind::PDF::Weekly - render a weekly calendar

=cut

sub render
{
        my ($self, $cr, $settings, $index, $total) = @_;
        if ($settings->{daynum_size} < 0) {
                $settings->{daynum_size} = 10;
        }
        if ($settings->{svg} || $settings->{eps}) {
                if ($index > $settings->{weeks_per_page}) {
                        if ($index == $settings->{weeks_per_page}+1) {
                                print STDERR "WARNING: --svg/--eps can only output one page; ignoring subsequent pages.\n";
                        }
                        return;
                }
        }
        if ((($index-1) % $settings->{weeks_per_page}) == 0) {
                my ($r, $g, $b) = @{$settings->{bg_color}};
                if ($r >= 0 && $g >= 0 && $b >= 0) {
                        $cr->save;
                        $self->set_cr_color($cr, $settings->{bg_color});
                        $cr->rectangle(0, 0, $settings->{width}, $settings->{height});
                        $cr->fill();
                        $cr->restore;
                }
        }

        $settings->{numbers_on_left} = 1;
        # Set up bounding box
        if ($settings->{weeks_per_page} == 1) {
                $self->{bounding_box} = [
                        $settings->{margin_left},
                        $settings->{margin_top},
                        $settings->{width} - $settings->{margin_right},
                        $settings->{height} - $settings->{margin_bottom}]
        } else {
                my $total_height = $settings->{height} - $settings->{margin_top} - $settings->{margin_bottom};
                my $week_height = $total_height / $settings->{weeks_per_page};
                my $top_offset = (($index-1) % $settings->{weeks_per_page}) * $week_height;
                my $bot_offset = $top_offset + $week_height;
                $self->{bounding_box} = 
                    $self->{bounding_box} = [
                            $settings->{margin_left},
                            $settings->{margin_top} + $top_offset,
                            $settings->{width} - $settings->{margin_right},
                            $settings->{margin_top} + $bot_offset];
                if ($index != 1) {
                        $self->{bounding_box}[1] += 0.25 * $settings->{margin_top};
                }
                if ($index != $settings->{weeks_per_page}) {
                    $self->{bounding_box}[3] -= 0.25 * $settings->{margin_top};
                }
        }
        $self->draw_headings($cr, $settings);
        for (my $i=0; $i<7; $i++) {
                $self->draw_entries($cr, $settings, $i);
        }
        $self->draw_lines($cr, $settings);
        if ($index == $total || ($index % $settings->{weeks_per_page}) == 0) {
                $cr->show_page();
        }

        if ($settings->{verbose}) {
                print STDERR "rem2pdf: Rendered " . $self->{dates}->[0]->{date} . " to " . $self->{dates}->[6]->{date} . "\n";
        }
}

sub draw_headings
{
        my ($self, $cr, $settings) = @_;
        my $ymax = 0;
        my $cell = ($settings->{width} - $settings->{margin_left} - $settings->{margin_right})/7;

        for (my $i=0; $i<7; $i++) {
                my $date = $self->{dates}[$i];
                my $month = $date->{month};
                my $year = $date->{year};
                my $day = $date->{day};
                my $dayname = $date->{dayname};

                my $layout = Pango::Cairo::create_layout($cr);
                $layout->set_text(Encode::decode('UTF-8', $dayname));

                my $desc = Pango::FontDescription->from_string($settings->{header_font} . ' ' . $settings->{header_size} . 'px');
                $layout->set_font_description($desc);

                my ($wid, $h) = $layout->get_pixel_size();
                $cr->save;
                $self->set_cr_color($cr, $settings->{header_color});
                $cr->move_to($settings->{margin_left} + $i * $cell + $cell/2 - $wid/2, $self->{bounding_box}[1]);
                Pango::Cairo::show_layout($cr, $layout);
                $cr->restore();

                $layout = Pango::Cairo::create_layout($cr);
                $layout->set_text(Encode::decode('UTF-8', $day . " " . $month . " " . $year));
                my $es = $settings->{entry_size};
                if ($es > 8) {
                        $es = 8;
                }
                $desc = Pango::FontDescription->from_string($settings->{entry_font} . ' ' . $es . 'px');
                $layout->set_font_description($desc);

                my ($wid2, $h2) = $layout->get_pixel_size();
                $cr->save;
                $self->set_cr_color($cr, $settings->{header_color});
                $cr->move_to($settings->{margin_left} + $i * $cell + $cell/2 - $wid2/2, $self->{bounding_box}[1] + $h);
                Pango::Cairo::show_layout($cr, $layout);
                $cr->restore();

                if ($h + $h2 > $ymax) {
                        $ymax = $h + $h2;
                }
        }
        $self->{heading_bottom_y} = $ymax + $self->{bounding_box}[1];
}

sub draw_entries
{
        my ($self, $cr, $settings, $i) = @_;

        my $cell = ($settings->{width} - $settings->{margin_left} - $settings->{margin_right})/7;

        # Coordinates of box from line-to-line
        my $l2l_box = [$i * $cell + $settings->{margin_left},
                       $self->{heading_bottom_y},
                       ($i+1) * $cell + $settings->{margin_left},
                       $self->{bounding_box}[3]];

        # Coordinates of drawing-space box
        my $box = [$l2l_box->[0] + $settings->{border_size},
                   $l2l_box->[1] + $settings->{border_size},
                   $l2l_box->[2] - $settings->{border_size},
                   $l2l_box->[3] - $settings->{border_size}];

        $self->{l2l_box} = $l2l_box;
        $self->{box} = $box;

        # Do shading, if any
        my $shade = $self->find_last_special('shade', $self->{entries}->[$i]);
        if ($shade) {
                $cr->save;
                $cr->set_source_rgb($shade->{r} / 255,
                                    $shade->{g} / 255,
                                    $shade->{b} / 255);
                $cr->rectangle($l2l_box->[0], $l2l_box->[1],
                               $l2l_box->[2] - $l2l_box->[0],
                               $l2l_box->[3] - $l2l_box->[1]);
                $cr->fill();
                $cr->restore;
        }

        # Get the "day number" size to leave room for moon and week specials
        my $layout = Pango::Cairo::create_layout($cr);
        $layout->set_text("31");
        my $desc = Pango::FontDescription->from_string($settings->{daynum_font} . ' ' . $settings->{daynum_size} . 'px');

        $layout->set_font_description($desc);
        my ($wid, $h) = $layout->get_pixel_size();

        my $so_far = $box->[1] + $settings->{border_size};
        foreach my $entry (@{$self->{entries}->[$i]}) {
                if ($entry->isa('Remind::PDF::Entry::moon')) {
                        $so_far = $box->[1] + $h + $settings->{border_size};
                        last;
                }
        }

        my $box_height = $box->[3] - $box->[1];
        my $done = 0;
        foreach my $entry (@{$self->{entries}->[$i]}) {
                # Moon and week should not adjust height
                if ($entry->isa('Remind::PDF::Entry::moon') ||
                    $entry->isa('Remind::PDF::Entry::week')) {
                        $entry->render($self, $cr, $settings, $box->[1], $i, $i, $box_height);
                        next;
                }

                # An absolutely-positioned Pango markup should not adjust height
                # either
                if ($entry->isa('Remind::PDF::Entry::pango') &&
                    defined($entry->{atx}) && defined($entry->{aty})) {
                        $entry->render($self, $cr, $settings, $box->[1], $i, $i, $box_height);
                        next;
                }

                # Shade is done already
                if ($entry->isa('Remind::PDF::Entry::shade')) {
                        next;
                }
                if ($done) {
                        $so_far += $settings->{border_size};
                }
                $done = 1;
                my $h2 = $entry->render($self, $cr, $settings, $so_far, $i, $i, $box_height);
                $so_far += $h2;
        }
}

sub col_box_coordinates
{

        my ($self, $so_far, $col, $height, $settings) = @_;
        return (@{$self->{l2l_box}});
}



sub draw_lines
{
        my ($self, $cr, $settings) = @_;

        $cr->save;
        $self->set_cr_color($cr, $settings->{line_color});
        # Top horizonal line
        $cr->move_to($self->{bounding_box}[0], $self->{bounding_box}[1]);
        $cr->line_to($self->{bounding_box}[2], $self->{bounding_box}[1]);
        $cr->stroke();

        # Horizontal line below headings
        $cr->move_to($self->{bounding_box}[0], $self->{heading_bottom_y});
        $cr->line_to($self->{bounding_box}[2], $self->{heading_bottom_y});
        $cr->stroke();

        # Bottom horizontal line
        $cr->move_to($self->{bounding_box}[0], $self->{bounding_box}[3]);
        $cr->line_to($self->{bounding_box}[2], $self->{bounding_box}[3]);
        $cr->stroke();

        # Vertical lines
        my $w = ($settings->{width} - $settings->{margin_left} - $settings->{margin_right})/7;
        for (my $i=0; $i<=7; $i++) {
                my $x = $settings->{margin_left} + ($i * $w);
                $cr->move_to($x, $self->{bounding_box}[1]);
                $cr->line_to($x, $self->{bounding_box}[3]);
                $cr->stroke();
        }
        $cr->restore;
}

sub create_from_hash
{
        my ($class, $hash, $specials_accepted) = @_;
        bless $hash, $class;

        my $filtered_entries = [];
        my $date_to_index;
        for (my $i=0; $i<7; $i++) {
                $date_to_index->{$hash->{dates}[$i]->{date}} = $i;
        }

        for (my $i=0; $i<7; $i++) {
                $filtered_entries->[$i] = [];
        }
        foreach my $e (@{$hash->{entries}}) {
                if ($hash->accept_special($e, $specials_accepted)) {
                        my $index = $date_to_index->{$e->{date}};
                        push(@{$filtered_entries->[$index]}, Remind::PDF::Entry->new_from_hash($e));
                }
        }
        $hash->{entries} = $filtered_entries;
        return $hash;
}

1;

