#!/usr/bin/python
import traceback
import os
import sys
import json
import base64
import re
import argparse
import codecs
import locale

basePath = os.path.abspath(os.path.dirname(__file__))
sys.path.append(os.path.join(basePath, "swigConversions"))
import terminationCodes as termCodeEnum
import interestCodes as interestCodeEnum

sys.stdout = codecs.getwriter("utf-8")(sys.stdout)
sys.stderr = codecs.getwriter("utf-8")(sys.stderr)

UNPROCESSED_FILENAME = "translateRec.unprocessed.json"
WARN_FILENAME = "translateRec.warning.txt"
warnHandle = None

termCodeStrList = [code for code in dir(termCodeEnum) if not code.startswith("_")]
termCodeToStrDict = dict((eval("termCodeEnum." + code), code) for code in termCodeStrList)
intrCodeStrList = [code for code in dir(interestCodeEnum) if not code.startswith("_")]
intrCodeToStrDict = dict((eval("interestCodeEnum." + code), code) for code in intrCodeStrList)

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

def write_warning(string) :
	warnHandle.write(string + "\n")
	sys.stderr.write("TranslateRecords warning : %s\n" % string)

def ensure_sanitized(value) :
	if (type(value) == int) :
		return value
	elif (type(value) == bool) :
		return value
	elif (type(value) == list) :
		for i in range(len(value)) :
			value[i] = ensure_sanitized(value[i])
		return value
	else :
		assert type(value) == str or type(value) == unicode, str(type(value))

		# B/c Python treats control characters as ascii, explicitly replace
		value = value.replace("\x00", "\\x00")
		value = value.replace("\x01", "\\x01")
		value = value.replace("\x02", "\\x02")
		value = value.replace("\x03", "\\x03")
		value = value.replace("\x04", "\\x04")
		value = value.replace("\x05", "\\x05")
		value = value.replace("\x06", "\\x06")
		value = value.replace("\x07", "\\x07")
		value = value.replace("\x08", "\\x08")
		# 0x09 == \t
		# 0x0a == \n
		value = value.replace("\x0b", "\\x0b")
		value = value.replace("\x0c", "\\x0c")
		# 0x0d == \r
		value = value.replace("\x0e", "\\x0e")
		value = value.replace("\x0f", "\\x0f")

		value = value.replace("\x10", "\\x10")
		value = value.replace("\x11", "\\x11")
		value = value.replace("\x12", "\\x12")
		value = value.replace("\x13", "\\x13")
		value = value.replace("\x14", "\\x14")
		value = value.replace("\x15", "\\x15")
		value = value.replace("\x16", "\\x16")
		value = value.replace("\x17", "\\x17")
		value = value.replace("\x18", "\\x18")
		value = value.replace("\x19", "\\x19")
		value = value.replace("\x1a", "\\x1a")
		value = value.replace("\x1b", "\\x1b")
		value = value.replace("\x1c", "\\x1c")
		value = value.replace("\x1d", "\\x1d")
		value = value.replace("\x1e", "\\x1e")
		value = value.replace("\x1f", "\\x1f")

		value = value.replace("\x7f", "\\x7f")
		try :
			value.decode("utf-8")
			return value
		except :
			temp = str([value])
			assert temp[0] == "["
			assert temp[1] == "'" or temp[1] == '"'
			assert temp[-2] == "'" or temp[-2] == '"'
			assert temp[-1] == "]"
			temp = temp[2:-2]
			return temp.replace("\\r", "\r").replace("\\n", "\n")

def strip_key(key) :
	assert "RECKEY_" in key
	return key.replace("RECKEY_", "")

def sanitize_b64(string) :
	return ensure_sanitized(base64.b64decode(string))

def handle_raw_data(newRecord, rawArray) :
	listGrps = {}
	for anObj in rawArray :
		if (anObj["key"] == "RECKEY_LIST_RESP") :
			if (anObj["aux"] == None) :
				path = "/NO-PATH-RECORDED/"
			else :
				path = sanitize_b64(anObj["aux"])

			if (not path in listGrps) :
				listGrps[path] = {}

			assert "LIST_RESP" not in listGrps[path]
			listGrps[path]["LIST_RESP"] = sanitize_b64(anObj["base"])

		elif (anObj["key"] == "RECKEY_LIST_DONE") :
			path = sanitize_b64(anObj["aux"])
			if (not path in listGrps) :
				listGrps[path] = {}

			assert "LIST_DONE" not in listGrps[path]
			listGrps[path]["LIST_DONE"] = sanitize_b64(anObj["base"])

		elif (anObj["key"] == "RECKEY_LIST_DATA") :
			path = sanitize_b64(anObj["aux"])
			if (not path in listGrps) :
				listGrps[path] = {}

			assert "LIST_DATA" not in listGrps[path]
			listGrps[path]["LIST_DATA"] = sanitize_b64(anObj["base"])

		elif (anObj["key"] == "RECKEY_PASV_RESP") :
			path = sanitize_b64(anObj["aux"])
			if (path.endswith("robots.txt")) :
				newRecord["PASV_RESP_ROBOTS"] = {}
				newRecord["PASV_RESP_ROBOTS"]["aux"] = path
				newRecord["PASV_RESP_ROBOTS"]["base"] = sanitize_b64(anObj["base"])
			else :
				if (not path in listGrps) :
					listGrps[path] = {}

				if ("PASV_RESP" not in listGrps[path]) :
					listGrps[path]["PASV_RESP"] = sanitize_b64(anObj["base"])
				else :
					write_warning("Multiple PASV_RESP -- IP : %s -- path : %s" % (newRecord["IP"], path))
					listGrps[path]["PASV_RESP"] = "MULTIPLE FOUND"
		elif (anObj["key"] == "RECKEY_TOO_MUCH_DATA") :
			if (anObj["aux"] == None) :
				path = None
			else :
				path = sanitize_b64(anObj["aux"])
			newRecord["TOO_MUCH_DATA"] = {
										"path" : path,
										"data" : sanitize_b64(anObj["base"])}
		else :
			assert anObj["aux"] == None, anObj["key"]
			newRecord[strip_key(anObj["key"])] = sanitize_b64(anObj["base"])

	newRecord["LIST_GRPS"] = []
	for aPath, aGrp in listGrps.items() :
		aGrp["path"] = aPath
		newRecord["LIST_GRPS"].append(aGrp)

def handle_uncrawled_array(newRecord, uncrawledArray) :
	newRecord["UNCRAWLED"] = []
	for anEle in uncrawledArray :
		newRecord["UNCRAWLED"].append(sanitize_b64(anEle))

def handle_interest_mask(newRecord, intrMask) :
	newRecord["INTEREST_MASK"] = ensure_sanitized(intrMask)
	newRecord["META_INTEREST_MASK"] = []
	for intrValue, intrName in intrCodeToStrDict.items() :
		if (intrMask & intrValue) :
			newRecord["META_INTEREST_MASK"].append(intrName)

def handle_term_code(newRecord, termCode) :
	newRecord["TERM_CODE"] = ensure_sanitized(termCode)
	newRecord["TERM_CODE_REASON"] = termCodeToStrDict[termCode]

def handle_term_desc(newRecord, encodedDesc) :
	newRecord["TERM_DESC"] = sanitize_b64(encodedDesc)

def translate_raw_record(line) :
	obj = json.loads(line)
	newRecord = {}
	newRecord["IP"] = obj["RECKEY_IP"]
	for key, value in obj.items() :
		if (key == "rawdata") :
			handle_raw_data(newRecord, value)
		elif (key == "RECKEY_UNCRAWLED") :
			handle_uncrawled_array(newRecord, value)
		elif (key == "misc") :
			newRecord["MISC"] = ensure_sanitized(value)
		elif (key == "RECKEY_INTEREST_MASK") :
			handle_interest_mask(newRecord, value)
		elif (key == "RECKEY_TERM_CODE") :
			handle_term_code(newRecord, value)
		elif (key == "RECKEY_TERM_DESC") :
			handle_term_desc(newRecord, value)
		else :
			newRecord[strip_key(key)] = ensure_sanitized(value)
	return newRecord

def main() :
	args = parse_cmd()
	unprocessed = open(os.path.join(args.outputDir, UNPROCESSED_FILENAME), "w")
	global warnHandle
	warnHandle = open(os.path.join(args.outputDir, WARN_FILENAME), "w")
	line = sys.stdin.readline().strip()
	while (line) :
		if (line == "DONE") :
			print "DONE"
			break
		try :
			newRecord = translate_raw_record(line)
			print json.dumps(newRecord)
		except :
			sys.stderr.write("*" * 50 + "\n")
			if (args.verbose) :
				sys.stderr.write(str(sys.exc_info()[0]))
				sys.stderr.write(str(traceback.format_exc()))
				sys.stderr.write("TranslateRecords error : " + \
							"A hard-error was encounter for " + \
							"IP %s " % json.loads(line)["RECKEY_IP"] + \
							"(record not processed)\n")
			else :
				sys.stderr.write("TranslateRecords error : A surpressed error occurred\n")
			sys.stderr.write("*" * 50 + "\n")
			unprocessed.write(line + "\n")
		line = sys.stdin.readline().strip()
	unprocessed.close()
	warnHandle.close()

main()
