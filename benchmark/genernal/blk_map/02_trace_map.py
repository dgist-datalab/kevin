#!/usr/local/bin/pypy
import sys


def main(filename):
    with open(filename) as f:
        raw = f.readlines()
    with open("blk_map.txt") as f:
        ranges = f.readlines()
    
    ranges, journal_range = ranges[:-1], ranges[-1].split()[1:]
    ranges.sort(key=lambda x: x.split()[1])
    ranges = [line.split() for line in ranges]

    def cont(value):
        for line in ranges:
            if value >= int(line[1]) and value <= int(line[2]): return {1: 'SB', 2: 'GD', 3: 'BB', 4: 'IB', 5: 'IT'}[int(line[0])]
        return "DE"

    def cont_ahead(value):
        for line in ranges:
            if value >= int(line[1]) and value <= int(line[2]): return {1: 'SB', 2: 'GD', 3: 'BB', 4: 'IB', 5: 'IT'}[int(line[0])]
        return "DE"    

    count_r = {'SB': 0, 'GD': 0, 'BB': 0, 'IB': 0, 'IT': 0, 'DE': 0, 'DA': 0}
    count_w = {'SB': 0, 'GD': 0, 'BB': 0, 'IB': 0, 'IT': 0, 'DE': 0, 'DA': 0, 'DJ': 0}
    count = {'R': count_r, 'W': count_w, 'D': 0}
    
    for line in raw:
        tp, lba, cnt = line.split()
        if 'M' in tp or 'RA' in tp:
            count[tp[0]][cont(int(lba))] += int(cnt)
        elif 'D' in tp:
            count['D'] += int(cnt)
        else:
            if int(lba) >= int(journal_range[0]) and int(lba) <= int(journal_range[1]):
                count['W']['DJ'] += int(cnt)
            else:
                count[tp[0]]['DA'] += int(cnt)

    count_r, count_w = count['R'], count['W']
    print(f"{count_r['SB']}\t{count_r['GD']}\t{count_r['BB']}\t{count_r['IB']}\t{count_r['IT']}\t{count_r['DE']}\t{count_r['DA']}\t{count_w['SB']}\t{count_w['GD']}\t{count_w['BB']}\t{count_w['IB']}\t{count_w['IT']}\t{count_w['DE']}\t{count_w['DA']}\t{count_w['DJ']}\t{count['D']}")


if __name__ == "__main__":
    main(sys.argv[1])
