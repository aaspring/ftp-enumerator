import json
import re
import os
import sys

basePath = os.path.abspath(os.path.dirname(__file__))
sys.path.append(os.path.join(basePath, "listLib"))
from listLib import *

PAD = "   "
DIR_TAG = "-| "
FILE_TAG= "-# "
GOOD_RESP_RE = "^(.*\n)*[12]\d\d.*(\r)?\n$"

class TreeNode :
	def __init__(self, path) :
		self.path = path
		self.subPaths = []

class Host :
	def __init__(self, jsonStr) :
		self.jsonStr = jsonStr
		self.baseObj = json.loads(jsonStr)
		self.dissectJson()
		self.warnings = []

	def dissectJson(self) :
		self.ip = self.baseObj["IP"]
		self.fsType = self.baseObj["FS_TYPE"]
		self.termCode = self.baseObj["TERM_CODE"]
		self.intrMask = self.baseObj["INTEREST_MASK"]
		self.disconnectRes = self.baseObj["DISCONNECT_RESULT"]
		self.pathToObjs = {}
		self.root = None
		self.layerDict = {}
		self.uncrawled = self.baseObj.get("UNCRAWLED", [])

		self.goodLists = []
		self.badListGrps = []
		if ("LIST_GRPS" in self.baseObj) :
			for aGrp in self.baseObj["LIST_GRPS"] :
				newList = ListData.new(
									self.fsType,
									self.ip,
									aGrp.get("path", "null path"),
									aGrp.get("LIST_DATA", "")
				)
				if (newList.is_parsable()) :
					self.goodLists.append(newList)
					assert not newList.path in self.pathToObjs
					depth = len(newList.path.split("/"))
					self.pathToObjs[newList.path] = {
													"group" : aGrp,
													"list" : newList,
													"node" : TreeNode(newList.path),
													"depth" : depth,
													"isAttached" : False}


					temp = self.layerDict.get(depth, [])
					temp.append(newList.path)
					self.layerDict[depth] = temp
				else :
					self.badListGrps.append(aGrp)

		# Build the file system tree
		if (len(self.goodLists) == 0) :
			return
		minDepth = min(self.layerDict)
		self.root = self.pathToObjs[self.layerDict[minDepth][0]]["node"]
		self.pathToObjs[self.layerDict[minDepth][0]]["isAttached"] = True

		pathQueue = [self.root.path]
		while (len(pathQueue) != 0) :
			curPath = pathQueue.pop(0)
			possSubPaths = self.layerDict.get(self.pathToObjs[curPath]["depth"] + 1, [])
			for aPossSubPath in possSubPaths :
				if (aPossSubPath.startswith(curPath)) :
					pathQueue.append(aPossSubPath)
					self.pathToObjs[curPath]["node"].subPaths.append(aPossSubPath)
					self.pathToObjs[aPossSubPath]["isAttached"] = True

	def get_banner(self) :
		return self.baseObj.get("BANNER", "")

	def get_ip(self) :
		return self.ip

	def get_term_code(self) :
		return self.termCode

	def get_intr_mask(self) :
		return self.intrMask

	def get_disconnect_result(self) :
		return self.disconnectRes

	def has_certs(self) :
		return "PEER_CERT" in self.baseObj

	def get_peer_cert(self) :
		return self.baseObj.get("PEER_CERT", "")

	def get_cert_chain(self) :
		return self.baseObj.get("CERT_CHAIN", "")

	def get_unparsable_list_groups(self) :
		return self.badListGrps

	def get_all_files(self) :
		temp = []
		for aList in self.goodLists :
			_, giantTuple = aList.get_database_dicts()
			fullList = giantTuple[3]
			for aDict in fullList :
				aDict.pop("nameIsBin", "")
				aDict.pop("realnameIsBin", "")
				aDict["path"] = giantTuple[1]
				temp.append(aDict)
		return temp

	def find_empty_reason(self, curNode, offset) :
		temp = ""
		if (not re.match(
				GOOD_RESP_RE,
				self.pathToObjs[curNode.path]["group"].get("LIST_RESP", ""))
		) :
			temp += PAD * (offset + 1) + "Error : LIST_RESP"
			temp += str([self.pathToObjs[curNode.path]["group"].get("LIST_RESP", "")])
			temp += "\n"

		if (not re.match(
				GOOD_RESP_RE,
				self.pathToObjs[curNode.path]["group"].get("LIST_DONE", ""))
		) :
			temp += PAD * (offset + 1) + "Error : LIST_DONE -- "
			temp += str([self.pathToObjs[curNode.path]["group"].get("LIST_DONE", "")])
			temp += "\n"

		if (not re.match(
				GOOD_RESP_RE,
				self.pathToObjs[curNode.path]["group"].get("PASV_RESP", ""))
		) :
			temp += PAD * (offset + 1) + "Error : PASV_RESP -- "
			temp += str([self.pathToObjs[curNode.path]["group"].get("PASV_RESP", "")])
			temp += "\n"

		if (temp != "") :
			return temp

		if ("LIST_DATA" in self.pathToObjs[curNode.path]["group"]) :
			if (self.pathToObjs[curNode.path]["group"]["LIST_DONE"] == "") :
				return PAD * (offset + 1) + "<<< Empty >>>\n"

		agree, strs = self.pathToObjs[curNode.path]["list"].agree_is_empty()
		if (agree) :
			return PAD * (offset + 1) + "<<< Empty >>>\n"
		else :
			for aStr in strs :
				temp += PAD * (offset + 1) + "META %s\n" % str([aStr])
			return temp

	def recur_get_file_system_string(self, curNode, offset) :
		string = ""
		string += PAD * offset + DIR_TAG + curNode.path + "\n"
		_, giantTuple = self.pathToObjs[curNode.path]["list"].get_database_dicts()

		notInTree = []
		if (len(giantTuple[3]) > 0) :
			for anEle in giantTuple[3] :
				if (anEle["isDir"]) :
					eleFullPath = os.path.join(curNode.path, anEle["name"]) + "/"
					if (not eleFullPath in self.pathToObjs) :
						notInTree.append(eleFullPath)
					continue
				string += PAD * (offset + 1) + FILE_TAG
				if (anEle["isLink"]) :
					string += "'%s' -> '%s' __ %s __ %d __ %s\n" % (
													anEle["name"],
													anEle["realname"],
													anEle["permis"],
													anEle["size"],
													anEle["time"])
				else :
					string += "'%s' __ %s __ %d __ %s\n" % (
													anEle["name"],
													anEle["permis"],
													anEle["size"],
													anEle["time"])
		else :
			string += self.find_empty_reason(curNode, offset)

		for aPath in curNode.subPaths :
			string += self.recur_get_file_system_string(
											self.pathToObjs[aPath]["node"],
											offset + 1)

		for aPath in notInTree :
			string += PAD * (offset + 1) + DIR_TAG
			string += aPath + "\n"
			if (aPath in self.uncrawled) :
				string += PAD * (offset + 2) + "<<< uncrawled >>>\n"
			else :
				string += PAD * (offset + 2) + "<<< Not in Tree >>>\n"
		return string

	def get_file_system_string(self) :
		string = "%s --- File System\n" % self.ip
		if (len(self.goodLists) == 0) :
			return "NO FILESYSTEM"

		string += self.recur_get_file_system_string(self.root, 0)
		string += "\n\n"
		for path, objs in self.pathToObjs.items() :
			if (not objs["isAttached"]) :
				string += "\tUnattached :\n"
				string += "\t\tPath -- %s\n" % objs["group"].get("path", "")
				string += "\t\tPASV_RESP -- %s\n" % str([objs["group"].get("PASV_RESP", "")])
				string += "\t\tLIST_RESP -- %s\n" % str([objs["group"].get("LIST_RESP", "")])
				string += "\t\tLIST_DONE -- %s\n" % str([objs["group"].get("LIST_DONE", "")])
				string += "\t\tLIST_DATA -- %s\n" % str([objs["group"].get("LIST_DATA", "")])

		string += "*" * 40 + "\n"
		return string

	def is_a_liar(self) :
		return "INTEREST_LIST_IS_LYING" in self.baseObj["META_INTEREST_MASK"]
