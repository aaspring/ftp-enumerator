from abc import ABCMeta, abstractmethod
import sys
import re
from shared import *
from File import *
from Dir import *

RESP_1XX_RE = "^1\d\d((.)|(\n))*$"
RESP_2XX_RE = "^2\d\d((.)|(\n))*$"

class ListData(object) :
	__metaclass__ = ABCMeta

	def __init__(self, ip, path, data) :
		self.ip = ip
		self.data = data
		self.agreeEmptyStrs = []

		self.ogPath = path

		try :
			path.decode("utf-8")
			self.pathIsBin = 0
		except :
			path = sanitize_path(path)
			self.pathIsBin = 1

		self.path = path
		assert self.path[-1] == "/", "%s -- %s" % (self.ip, self.path)

		temp = remove_empties(data.split("\n"))
		self.allLines = []
		for aLine in temp :
			if (aLine[-1] == "\r") :
				self.allLines.append(aLine[:-1])
			else :
				self.allLines.append(aLine)

		self.allLines = remove_empties(self.allLines)

		self.lines = []
		self.metaLines = []

		self.files = []
		self.dirs = []

		self.fs_type = UNSET
		self.listResp = UNSET
		self.listRespDone = UNSET
		self.isParsable = False

	def is_parsable(self) :
		return self.isParsable

	@staticmethod
	def new(inFsType, ip, path, data) :
		if (inFsType == "Unix") :
			if ("<DIR>" in data) :
				return WindowsListData(ip, path, data)
			else :
				return UnixListData(ip, path, data)
		elif (inFsType == "Windows") :
			temp = remove_empties(data.split("\n"))
			sp = []
			for aLine in temp :
				if (aLine[-1] == "\r") :
					sp.append(aLine[:-1])
				else :
					sp.append(aLine)
			sp = remove_empties(sp)
			if (
				(len(sp) != 0)
				and (sp[0][0] in "-ld")
			) :
				return UnixListData(ip, path, data)
			elif (
				(len(sp) != 0)
				and (
					(sp[0].startswith("total"))
					or (sp[0].startswith("Total"))
				)
			) :
				return UnixListData(ip, path, data)
			else :
				return WindowsListData(ip, path, data)
		elif (inFsType == "VxWorks") :
			temp = remove_empties(data.split("\n"))
			sp = []
			for aLine in temp :
				if (aLine[-1] == "\r") :
					sp.append(aLine[:-1])
				else :
					sp.append(aLine)
			sp = remove_empties(sp)
			if (
				(len(sp) != 0)
				and (sp[0][0] in "-ld")
			) :
				return UnixListData(ip, path, data)
			else :
				return VxWorksListData(ip, path, data)
		elif (
			(inFsType == "Unk")
			and (len(data) != 0)
			and (data[0] in "-ld")
		) :
			return UnixListData(ip, path, data)
		elif (inFsType == "Unk") :
			return UnixListData(ip, path, data)
		else :
			assert False, "ListData.new() can't classify : %s" % (
						"inFsType : %s\nip : %s\npath : %s\ndata : %s" % (
								inFsType,
								ip,
								path,
								data
						)
			)


	def get_base_debug_string(self) :
		string = ""
		string += self.ip + "\n"
		string += "\tType : " + self.fs_type + "\n"
		string += "\tPath : " + self.path + "\n"

		string += "\tMeta-lines : \n"
		for aLine in self.metaLines :
			string += "\t\t" + aLine + "\n"

		string += "\tLines : \n"
		for aLine in self.lines :
			string += "\t\t" + aLine + "\n"

		return string

	@abstractmethod
	def get_debug_string(self) :
		pass

	@abstractmethod
	def break_lines(self) :
		pass

	def agree_is_empty(self) :
		if (len(self.agreeEmptyStrs) == 0) :
			return True, None
		else :
			return False, self.agreeEmptyStrs

	"""
	Return :	T, (ip, path, pathIsBin, [{dict of fields}, {dict of fields}, ..])
				F, (ip, path, data)
	"""
	def get_database_dicts(self) :
		if (not self.isParsable) :
			return False, (self.ip, self.ogPath, self.data)

		tupleList = []
		for aFile in self.files :
			tupleList.append(aFile.get_database_dict())

		for aDir in self.dirs :
			tupleList.append(aDir.get_database_dict())

		return True, (self.ip, self.path, self.pathIsBin, tupleList)

class UnixListData(ListData) :
	TOTAL_LINE_RE = "^[tT]otal \d+(\.)?$"
	LINUX_NOT_FOUND = "^/.* +not found$"
	LINUX_NOT_FOUND_2 = "^(((ftpd)|(ls))(: ))?.+: No such file or directory(\.)?$"
	LINUX_PERMIS_DENIED = "^((ls)|(ftpd))?(: )?(/.+)?Permission denied$"
	LINUX_DEFAULT_PRINTER = "defaultPrinter"
	LINUX_EOF = "^%%EOF$"
	LINUX_DESC_LINE = "^(/[-a-zA-Z0-9_]+)+:$"
	LINUX_IO_ERROR = "^213-Error: Input/output error\.$"
	LINUX_DOT_EMPTY = "^\[\.\]$"

	def __init__(self, ip, path, data) :
		ListData.__init__(self, ip, path, data)
		self.fs_type = "Unix"
		self.break_lines()

	def break_lines(self) :
		for aLine in self.allLines :
			if (re.match(self.TOTAL_LINE_RE, aLine)) :
				self.metaLines.append(aLine)
			elif (
				(aLine.strip().endswith(" ."))
				or (aLine.strip().endswith(" .."))
			) :
				# cur/prev lines
				self.metaLines.append(aLine)
			elif (
				(aLine.strip() == ".")
				or (aLine.strip() == "..")
			) :
				# Stupid printers
				self.metaLines.append(aLine)
			elif (re.match(self.LINUX_NOT_FOUND, aLine)) :
				self.metaLines.append(aLine)
				self.agreeEmptyStrs.append(aLine)
			elif (re.match(self.LINUX_NOT_FOUND_2, aLine)) :
				self.metaLines.append(aLine)
				self.agreeEmptyStrs.append(aLine)
			elif (re.match(self.LINUX_PERMIS_DENIED, aLine)) :
				self.metaLines.append(aLine)
				self.agreeEmptyStrs.append(aLine)
			elif (re.match(self.LINUX_DEFAULT_PRINTER, aLine)) :
				self.metaLines.append(aLine)
			elif (re.match(self.LINUX_EOF, aLine)) :
				self.metaLines.append(aLine)
			elif (re.match(self.LINUX_DESC_LINE, aLine)) :
				self.metaLines.append(aLine)
			elif (re.match(self.LINUX_IO_ERROR, aLine)) :
				self.metaLines.append(aLine)
				self.agreeEmptyStrs.append(aLine)
			elif (re.match(self.LINUX_DOT_EMPTY, aLine)) :
				self.metaLines.append(aLine)
			else :
				self.lines.append(aLine)
				if (self.line_is_dir(aLine)) :
					newDir = UnixDir(self.path, aLine)
					if (newDir.is_parsable()) :
						self.dirs.append(newDir)
					else :
						self.isParsable = False
						return
				else :
					newFile = UnixFile(aLine)
					if (newFile.is_parsable()) :
						self.files.append(newFile)
					else :
						self.isParsable = False
						return
		self.isParsable = True

	def line_is_dir(self, aLine) :
		if (aLine[0] == "d") :
			return True
		elif ((aLine[0] == "l") and (aLine.strip()[-1] == "/")) :
			return True
		else :
			return False

	def get_debug_string(self) :
		string = ListData.get_base_debug_string(self)

		string += "\tFiles ::\n"
		for aFile in self.files :
			string += aFile.get_debug_string()

		string += "\tDirs ::\n"
		for aDir in self.dirs :
			string += aDir.get_debug_string()
		return string

class WindowsListData(ListData) :

	def __init__(self, ip, path, data) :
		ListData.__init__(self, ip, path, data)
		self.fs_type = "Windows"
		self.break_lines()

	def break_lines(self) :
		for aLine in self.allLines :
			if (aLine == "") :
				continue

			self.lines.append(aLine)
			if (self.line_is_dir(aLine)) :
				newDir = WindowsDir(self.path, aLine)
				if (newDir.is_parsable()) :
					self.dirs.append(newDir)
				else :
					self.isParsable = False
					return
			else :
				newFile = WindowsFile(aLine)
				if (newFile.is_parsable()) :
					self.files.append(newFile)
				else :
					self.isParsable = False
					return
		self.isParsable = True

	def line_is_dir(self, aLine) :
		return "<DIR>" in aLine

	def get_debug_string(self) :
		string = ListData.get_base_debug_string(self)

		string += "\tFiles ::\n"
		for aFile in self.files :
			string += aFile.get_debug_string()

		string += "\tDirs ::\n"
		for aDir in self.dirs :
			string += aDir.get_debug_string()
		return string

class VxWorksListData(ListData) :
	VXWORKS_HEADER_1 = "^  size          date       time       name$"
	VXWORKS_HEADER_2 = "^--------       ------     ------    --------$"
	VXWORKS_CANT_OPEN = "^Can't open .*$"

	def __init__(self, ip, path, data) :
		ListData.__init__(self, ip, path, data)
		self.fs_type = "VxWorks"
		self.break_lines()

	def break_lines(self) :
		for aLine in self.allLines :
			if (re.match(self.VXWORKS_HEADER_1, aLine)) :
				self.metaLines.append(aLine)
			elif (re.match(self.VXWORKS_HEADER_2, aLine)) :
				self.metaLines.append(aLine)
			elif (re.match(self.VXWORKS_CANT_OPEN, aLine)) :
				self.metaLines.append(aLine)
				self.agreeEmptyStrs.append(aLine)
			elif (
				re.match("^.+ +\. +<DIR>( )*$", aLine)
				or re.match("^.+ +\.\. +<DIR>( )*$", aLine)
			) :
				self.metaLines.append(aLine)
			else :
				self.lines.append(aLine)
				if (self.line_is_dir(aLine)) :
					newDir = VxWorksDir(self.path, aLine)
					if (newDir.is_parsable()) :
						self.dirs.append(newDir)
					else :
						self.isParsable = False
						return
				else :
					newFile = VxWorksFile(aLine)
					if (newFile.is_parsable()) :
						self.files.append(newFile)
					else :
						self.isParsable = False
						return
		self.isParsable = True

	def line_is_dir(self, aLine) :
		return aLine.strip().endswith("<DIR>")

	def get_debug_string(self) :
		string = ListData.get_base_debug_string(self)

		string += "\tFiles ::\n"
		for aFile in self.files :
			string += aFile.get_debug_string()

		string += "\tDirs ::\n"
		for aDir in self.dirs :
			string += aDir.get_debug_string()
		return string

if (__name__ == "__main__") :
	listObj = ListData.new("Unix", "1.1.1.1", "/",
		"drwxr-xr-x  22 root root  4096 13. Jun 16:40 .\r\n" + \
		"drwxr-xr-x  22 root root  4096 13. Jun 16:40 ..\r\n" + \
		"drwxr-xr-x   2 root root  4096 13. Jun 15:51 bin\r\n" + \
		"drwxr-xr-x   3 root root  4096 13. Jun 16:05 boot\r\n" + \
		"drwxr-xr-x  15 root root  3740 13. Jun 16:41 dev\r\n" + \
		"drwxr-xr-x 110 root root 12288 13. Jun 16:40 etc\r\n" + \
		"drwxr-xr-x   4 root root  4096 13. Jun 16:07 home\r\n" + \
		"drwxr-xr-x  20 root root 12288 13. Jun 15:54 lib\r\n" + \
		"drwxr-xr-x   2 root root  4096 18. Feb 14:48 media\r\n" + \
		"drwxr-xr-x   2 root root  4096 18. Feb 14:48 mnt\r\n" + \
		"drwxr-xr-x   2 root root  4096 18. Feb 14:48 opt\r\n" + \
		"dr-xr-xr-x 133 root root     0 13. Jun 16:40 proc\r\n" + \
		"drwx------   7 root root  4096 13. Jun 16:51 root\r\n" + \
		"drwxr-xr-x   3 root root 12288 13. Jun 15:52 sbin\r\n" + \
		"drwxr-xr-x   2 root root  4096 18. Feb 14:48 selinux\r\n" + \
		"drwxr-xr-x   5 root root  4096 13. Jun 15:49 srv\r\n" + \
		"drwxr-xr-x  12 root root     0 13. Jun 16:40 sys\r\n" + \
		"drwxrwxrwt  10 root root  4096 13. Jun 16:45 tmp\r\n" + \
		"drwxr-xr-x  12 root root  4096 02. Mar 13:14 usr\r\n" + \
		"drwxr-xr-x  15 root root  4096 02. Mar 13:14 var\r\n"
	)
	print listObj.get_debug_string()

