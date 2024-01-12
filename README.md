
# ZBANNER: completely stateless port scanner and banner grabber

ZBanner is an Internet-scale port scanner and banner grabber.
It scans port and grabs banner in completely stateless mode.
Of cause asychronously too.

The tool is developed based on the code of [Masscan](https://github.com/robertdavidgraham/masscan) and some thoughts of [ZMap](https://github.com/zmap/zmap).
These two are both great tools and still maintained now.
I have always held both authors in high esteem because of the many knowledge and skills I have learned from their code.

ZBanner is as a sub-function of Masscan until now and has most of the capabilities of Masscan.
Actually some main code was modified and new things were added, also I will drop many no stateless functions in the future.
However, you'd better know the basic method and usages of Masscan.
Contents only about ZBanner are followed.

## Completely stateless Mode

Unlike scan on application-layer with TCP/IP stack of system or user mode,
ZBanner sends application-layer probes and obtain banners completely stateless after the target port was identified open (received SYNACK).

## Stateless Probe Module

Aka application-layer request module.

Achieve what you want by implementing your StatelessProbe.

Possible achievements:

- Get banner of specific protocol;
- Service identification;
- Detect application-layer vuln;
- Integrate other probes;
- etc.

## Multi Transmiting Threads

Multi-threads model was rewritten and I dropped supporting for multi NICs of Masscan.
ZBanner supports any number of transmiting threads just like ZMap now and is as fast as it.

## Main Usages of ZBanner

Do TCP SYN scan and get banners statelessly if ports are open:

```
masscan 10.0.0.0/8 -p110 --stateless
```

Specify rate(pps) and time(sec) to wait after done:

```
masscan 10.0.0.0/8 -p110 --stateless --rate 300000 --wait 15
```

Specify application-layer probe:

```
masscan 10.0.0.0/8 -p80 --stateless --probe getrequest
```

List all application-layer probes:

```
masscan --list-probes
```

Output banner in result:

```
masscan 10.0.0.0/8 -p110 --stateless --capture stateless
```

The captured "Banner" could be any results of probe's verification.

Save receive packets to pcap file for analysis:

```
masscan 10.0.0.0/8 -p110 --stateless --pcap result.pcap
```

Also save status output:

```
masscan 10.0.0.0/8 -p110 --stateless --pcap result.pcap -oX result.xml
```

Set deduplication window for SYN-ACK:

```
masscan 10.0.0.0/8 -p110 --dedupwin1 65535
```

Set deduplication window for response with data:

```
masscan 10.0.0.0/8 -p110 --stateless --dedupwin2 65535
```

Also use `--dedupwin` to set both window. Default win are 100M.

Do not deduplicating for SYN-ACK:

```
masscan 10.0.0.0/8 -p110 --nodedup1
```

Do not deduplicating for response with data:

```
masscan 10.0.0.0/8 -p110 --stateless --nodedup2
```

Also use `--nodedup` to ban deduplicating for all.

Do not send RST for SYN-ACK:

```
masscan 10.0.0.0/8 -p110 --noreset1
```

Do not send RST for response with data:

```
masscan 10.0.0.0/8 -p110 --stateless --noreset2
```

Also use `--noreset` to ban reset for all.

Work with LZR:

```
masscan 10.0.0.0/8 -p 80 --noreset1 --feedlzr | \
lzr --handshakes http -sendInterface eth0 -f results.json
```

Use multi transmit thread:

```
masscan 10.0.0.0/8 -p110 --noreset1 --tx-count 3
```

use `--stack-buf-count` to set callback queue and packet buffer entries count:

```
masscan 10.0.0.0/8 -p110 --stack-buf-count 2048
```

`--stack-buf-count` must be power of 2 and do not exceed RTE_RING_SZ_MASK.


## Tips

1. Do not use stateless-banners mode with `--banners` mode.

2. Use default null probe while no probe was specified.

3. Supported output and save method in stateless mode:
    - output to stdout;
    - output to XML file(`-oX`): most detailed result with statistic info;
    - output to grepable file(`-oG`);
    - output to json file(`-oJ`);
    - output to list file(`-oL`);
    - output to binary file(`-oB`): a light and special format of masscan with info like list file.
    Can not keep 'reponsed' state data in stateless mode.
    - save a pcap file(`--pcap`): raw result for later analysis.

4. Statistic result `responsed`(aka a PortStatus) is number of responsed target in application-layer.
It's useful just in stateless mode.

5. Use `--interactive` to also print result to cmdline while saving to file.

6. Use `--nostatus` to switch off status printing.

7. Use `--ndjson-status` to get status in details.

# Authors

The original Masscan was created by Robert Graham:
email: robert_david_graham@yahoo.com
twitter: @ErrataRob

ZBanner was written by lfishRhungry:
email: shineccy@aliyun.com

# License

Copyright (c) 2023 lfishRhungry

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, version 3 of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
