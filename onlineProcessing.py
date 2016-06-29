#!/usr/bin/python
import traceback
import base64
import operator
import json
import sys
import argparse
import os

basePath = os.path.abspath(os.path.dirname(__file__))
sys.path.append(os.path.join(basePath, "pythonLibs"))
sys.path.append(os.path.join(basePath, "swigConversions"))

from Host import *
import terminationCodes as termCodeEnum
import interestCodes as interestCodeEnum
from interestingFileRe import *

ERROR_FILENAME = "onlineProc.err.txt"
SMILEY_FILENAME = "onlineProc.smileyVuln.txt"
BASE_METRICS_FILENAME = "onlineProc.baseMetrics.txt"
UNPROCESSED_FILENAME = "onlineProc.unprocessed.json"
UNPARSABLE_LIST_FILENAME = "onlineProc.unparsableLists.json"
FILES_FILENAME = "onlineProc.files.json"

attrib = {}

def parse_cmd() :
	parser = argparse.ArgumentParser()
	parser.add_argument(
		"-o",
		"--outputDir",
		help = "Directory to write output files to",
		required = True,
		dest = "outputDir"
	)

	parser.add_argument(
		"-v",
		"--verbose",
		help = "Print more info on errors",
		required = False,
		dest = "verbose",
		action = "store_true",
		default = False
	)

	return parser.parse_args()

def setup_env(args) :
	# Open all the file handles
	attrib["errHandle"] = open(os.path.join(args.outputDir, ERROR_FILENAME), "w")
	attrib["smileyHandle"] = open(os.path.join(args.outputDir, SMILEY_FILENAME), "w")
	attrib["baseMetricHandle"] = open(os.path.join(args.outputDir, BASE_METRICS_FILENAME), "w")
	attrib["unProcHandle"] = open(os.path.join(args.outputDir, UNPROCESSED_FILENAME), "w")
	attrib["unparsableListsHandle"] = open(os.path.join(args.outputDir, UNPARSABLE_LIST_FILENAME), "w")
	attrib["filesHandle"] = open(os.path.join(args.outputDir, FILES_FILENAME), "w")

	# Read in the C-structs
	termCodeStrList = [code for code in dir(termCodeEnum) if not code.startswith("_")]
	attrib["termCodeToStr"] = dict((eval("termCodeEnum." + code), code) for code in termCodeStrList)
	intrCodeStrList = [code for code in dir(interestCodeEnum) if not code.startswith("_")]
	attrib["intrCodeToStr"] = dict((eval("interestCodeEnum." + code), code) for code in intrCodeStrList)

	# Setup counters
	attrib["termCodeToCount"] = dict((eval("termCodeEnum." + code), 0) for code in termCodeStrList)
	attrib["intrCodeToCount"] = dict((eval("interestCodeEnum." + code), 0) for code in intrCodeStrList)
	attrib["quitTypeToCount"] = {"ABRUPT" : 0, "PLEASANT" : 0, "RUDE" : 0}
	attrib["numHosts"] = 0.0
	attrib["numSmileyVuln"] = 0

def close_env(args) :
	# Write term-code metrics
	handle = attrib["baseMetricHandle"]
	sortedValueCount = sorted(attrib["termCodeToCount"].items(), key = operator.itemgetter(1))[::-1]
	handle.write("%3s%27s\t%10s\t%10s\n" % (
								"Cnt",
								"Termination Code",
								"Count",
								"Percentage(of all hosts crawled)"
	))
	handle.write("-" * 80 + "\n")
	for aValue in sortedValueCount :
		if (aValue[1] != 0) :
			handle.write("%3d%27s\t%10d\t%.2f\n" % (
										aValue[0],
										attrib["termCodeToStr"][aValue[0]],
										aValue[1],
										float(aValue[1]) / attrib["numHosts"]
			))
	handle.write("\n\n")

	# Write intr mask metrics
	sortedValueCount = sorted(attrib["intrCodeToCount"].items(), key = operator.itemgetter(1))[::-1]
	handle.write("%30s\t%10s\t%10s\n" % ("Interest Code", "Count", "Percentage(of all hosts crawled)"))
	handle.write("-" * 80 + "\n")
	for aValue in sortedValueCount :
		if (aValue[1] != 0) :
			handle.write("%30s\t%10d\t%.2f\n" % (
										attrib["intrCodeToStr"][aValue[0]],
										aValue[1],
										float(aValue[1]) / attrib["numHosts"]
			))
	handle.write("\n\n")

	# Write termination code metrics
	handle.write("%30s\t%10s\t%10s\n" % ("Disconnect Value", "Count", "Percentage(of all hosts crawled)"))
	handle.write("-" * 80 + "\n")
	total = 0
	for aType, count in attrib["quitTypeToCount"].items() :
		total += int(count)
		handle.write("%30s\t%10d\t%.2f\n" % (
									aType,
									count,
									float(count) / attrib["numHosts"]
		))
	handle.write("\nTotal num crawled : %d\n" % total)
	handle.close()
	handle = None


	# Write little small things
	attrib["smileyHandle"].write("\n\tTotal : %d\n" % attrib["numSmileyVuln"])

	attrib["errHandle"].close()
	attrib["smileyHandle"].close()
	attrib["baseMetricHandle"].close()
	attrib["unProcHandle"].close()
	attrib["unparsableListsHandle"].close()
	attrib["filesHandle"].close()

def write_error(string) :
	attrib["errHandle"].write(string + "\n")
	sys.stderr.write("OnlineProcessing error : %s\n" % string)

def check_smiley_vuln(host) :
	banner = host.get_banner()
	if (
		("vsftpd" in banner.lower())
		and ("2.3.4" in banner)
	) :
		attrib["smileyHandle"].write(host.get_ip() + "\n")
		attrib["numSmileyVuln"] += 1

def update_base_metrics(host) :
	attrib["termCodeToCount"][host.get_term_code()] += 1
	attrib["quitTypeToCount"][host.get_disconnect_result()] += 1
	mask = host.get_intr_mask()
	for aTest in attrib["intrCodeToCount"] :
		if (mask & aTest) :
			attrib["intrCodeToCount"][aTest] += 1

def output_certs(host) :
	if (host.has_certs()) :
		aDict = {"IP" : host.get_ip()}
		aDict["PEER_CERT"] = host.get_peer_cert()
		aDict["CERT_CHAIN"] = host.get_cert_chain()
		print json.dumps(aDict)

def output_files(host) :
	unparsables = host.get_unparsable_list_groups()
	if (len(unparsables) != 0) :
		outObj = {"IP" : host.get_ip()}
		outObj["LIST_GRPS"] = unparsables
		attrib["unparsableListsHandle"].write(json.dumps(outObj) + "\n")

	allFiles = host.get_all_files()
	if (len(allFiles) != 0) :
		outObj = {"IP" : host.get_ip()}
		outObj["FILES"] = allFiles
		attrib["filesHandle"].write(json.dumps(outObj) + "\n")

def main() :
	args = parse_cmd()
	setup_env(args)
	line = sys.stdin.readline().strip()
	while (line) :
		if (line == "DONE") :
			print "DONE"
			break
		attrib["numHosts"] += 1.0
		try :
			host = Host(line)
			update_base_metrics(host)
			check_smiley_vuln(host)
			output_files(host)
			output_certs(host)
		except :
			sys.stderr.write("*" * 50 + "\n")
			if (args.verbose) :
				write_error(str(sys.exc_info()[0]))
				write_error(str(traceback.format_exc()))
				write_error("A hard-error was encounter for " + \
							"IP %s " % json.loads(line)["IP"] + \
							"(record not processed)")
			else :
				write_error("OnlineProcessing : A surpressed error occurred\n")
			sys.stderr.write("*" * 50 + "\n")
			attrib["unProcHandle"].write(line + "\n")
		line = sys.stdin.readline().strip()

	close_env(args)

main()
