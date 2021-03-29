#!/usr/bin/python3

NUM_BLKS_IN_GROUP = 16
INODETABLE_SIZE = 512

def main():
    with open('dump.txt') as f:
        raw = f.readlines()
    
    f = open('blk_map.txt', 'w')
    
    # Superblock section
    #f.write("SB\n")
    for line in raw:
        line = line.split()
        if len(line) > 5 and line[1] == 'superblock':
            f.write(f"1 { line[3][:-1] } { line[3][:-1] }\n")

    # Descriptor section
    #f.write("GD\n")
    for line in raw:
        line = line.split()
        if len(line) > 5 and line[4] == "Group":
            f.write(f"2 { ' '.join(line[-1].split('-')) }\n")

    # Block bitmap section
    #f.write("BB\n")
    for num, line in enumerate(raw):
        line = line.split()
        if len(line) > 5 and line[0] == "Group" and not (int(line[1][:-1]) % NUM_BLKS_IN_GROUP):
            for subline in raw[num+1:num+4]:
                subline = subline.split()
                if subline[0] == "Block":
                    f.write(f"3 { ' '.join([subline[3], str(int(subline[3]) + NUM_BLKS_IN_GROUP - 1)]) }\n")

    # Inode bitmap section
    #f.write("IB\n")
    for num, line in enumerate(raw):
        line = line.split()
        if len(line) > 5 and line[0] == "Group" and not (int(line[1][:-1]) % NUM_BLKS_IN_GROUP):
            for subline in raw[num+2:num+5]:
                subline = subline.split()
                if subline[0:2] == ["Inode", "bitmap"]:
                    f.write(f"4 { ' '.join([subline[3], str(int(subline[3]) + NUM_BLKS_IN_GROUP - 1)]) }\n")

    # Inode table section
    #f.write("IT\n")
    for num, line in enumerate(raw):
        line = line.split()
        if len(line) > 5 and line[0] == "Group" and not (int(line[1][:-1]) % NUM_BLKS_IN_GROUP):
            for subline in raw[num+3:num+6]:
                subline = subline.split()
                if subline[0:2] == ["Inode", "table"]:
                    f.write(f"5 { ' '.join([subline[3].split('-')[0], str(int(subline[3].split('-')[0]) + INODETABLE_SIZE * NUM_BLKS_IN_GROUP - 1)]) }\n")
   
if __name__ == "__main__":
    main()
