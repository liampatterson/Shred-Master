from mido import MidiFile, Message, MidiTrack, MetaMessage

filename = "the_strokes-reptilia-guitar"

mid = MidiFile('midi/'+filename+'.mid')

#new_str = ""

notes_on = ""

notes_list = ""

notes = 0
notes_mod = 0
final_notes_list = []
ones = 0
twos = 0
threes = 0
fours = 0
fives = 0

notes_list2 = []

for entries in mid.tracks[0v]:
    new_str = str(entries)
    #print new_str
    note_onoff = new_str[0:8]
    if ( note_onoff == "note_on " ):
        #notes_on = notes_on + str(entries) + '\n'
	#print new_str[23:25]
	notes_list2.append(int(new_str[23:25]))
        notes_list = new_str[23:25]
        notes = int(notes_list)
        notes_mod = notes % 12
        if ( notes_mod <= 1 ):
            final_notes_list.append(1)
            ones = ones + 1
        elif ( notes_mod <= 5 & notes_mod >=2 ):
            final_notes_list.append(2)
            twos = twos + 1
        elif ( notes_mod <= 7 & notes_mod >=6 ):
            final_notes_list.append(3)
            threes = threes + 1
        elif ( notes_mod == 8 ):
            final_notes_list.append(4)
            fours = fours + 1
        else:
            final_notes_list.append(5)
            fives = fives + 1
#print notes_list2
#print final_notes_list
file = open( filename+".txt", "w" )
for i in range(len(notes_list2)/2):
    file.write(str("{ ") + str(final_notes_list[i*2]) + str(", ") + str(notes_list2[i*2]) + str(" }, \n"))
#print str(ones) + ", " + str(twos) + ", " + str(threes) + ", " + str(fours) + ", " + str(fives)
