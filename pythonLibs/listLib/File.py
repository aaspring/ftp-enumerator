from abc import ABCMeta, abstractmethod
import sys
import time
import re
from shared import *

class File(object) :
	__metaclass__ = ABCMeta

	def __init__(self) :
		self.name = UNSET
		self.permis = UNSET
		self.size = UNSET
		self.isLink = False
		self.realname = UNSET
		self.isParsable = False
		self.nameIsBin = UNSET
		self.realnameIsBin = UNSET
		self.time = UNSET

	def is_parsable(self) :
		return self.isParsable

	@abstractmethod
	def parse_line(self, line) :
		pass

	def get_base_debug_line(self) :
		string = ""
		string += "\tName : " + self.name + "\n"
		string += "\t\tPermiss : " + self.permis + "\n"
		string += "\t\tSize : " + self.size + "\n"

		string += "\t\tLink? : " + str(self.isLink) + "\n"
		if (self.isLink) :
			string += "\t\t\tFake name : " + self.name + "\n"
			string += "\t\t\tReal name : " + self.realname + "\n"

		return string

	@abstractmethod
	def get_debug_string(self) :
		pass

	def get_export_line(self) :
		if (self.isLink) :
			return "%s -> %s %s %s" % (
							self.name,
							self.realname,
							self.permis,
							self.size
			)
		else :
			return "%s %s %s" % (self.name, self.permis, self.size)

	def get_database_dict(self) :
		assert self.isParsable
		aDict = {}
		aDict["isDir"] = False
		aDict["isLink"] = self.isLink
		aDict["permis"] = self.permis
		aDict["size"] = int(self.size)
		aDict["name"] = self.name
		aDict["nameIsBin"] = self.nameIsBin
		aDict["realnameIsBin"] = self.realnameIsBin
		aDict["time"] = self.time
		if (self.isLink) :
			aDict["realname"] = self.realname
		else :
			aDict["realname"] = ""

		return aDict

	def set_time(self, day, month, year, hour, minutes, amPm) :
		if (int(hour) == 0) :
			hour = "1"

		if (month.isdigit()) :
			temp = time.strptime(month, "%m")
			month = time.strftime("%b", temp)
		elif (len(month) > 3) :
			month = month[:3]

		if (amPm == None) :
			if (int(hour) < 12) :
				amPm = "AM"
			elif (int(hour) == 12) :
				amPm = "PM"
			else :
				# Different format b/c 24-hour time
				timeStr = "%s-%s-%s %s:%s" % (
								day,
								month,
								year,
								hour,
								minutes,
								)
				timeObj = time.strptime(timeStr, "%d-%b-%Y %H:%M")
				self.time = time.strftime(ISO_TIME_FMT, timeObj)
				return

		timeStr = "%s-%s-%s %s:%s %s" % (
						day,
						month,
						year,
						hour,
						minutes,
						amPm
						)
		timeObj = time.strptime(timeStr, "%d-%b-%Y %I:%M %p")
		self.time = time.strftime(ISO_TIME_FMT, timeObj)



class UnixFile(File) :
	def __init__(self, line) :
		File.__init__(self)
		self.parse_line(line)
		if (self.isParsable) :
			self.parse_time(line)


	def parse_time(self, line) :
		if (re.match(TIME_LINUX_1_2_4_5_6_7, line)) :
			matchObj = re.match(TIME_LINUX_1_2_4_5_6_7, line)
			assert matchObj != None
			month = matchObj.group(1)
			if (month.isdigit()) :
				month = MONTH_ARRAY[int(month)]
			day = matchObj.group(2)
			yearTime = matchObj.group(3)
			if (":" in yearTime) :
				year = TIME_DEFAULT_YEAR
				sp = yearTime.split(":")
				hour = sp[0]
				minutes = sp[1]
			else :
				year = yearTime
				hour = "0"
				minutes = "0"
			self.set_time(day, month, year, hour, minutes, None)
		elif (re.match(TIME_LINUX_3, line)) :
			matchObj = re.match(TIME_LINUX_3, line)
			assert matchObj != None
			day = matchObj.group(1)
			month = matchObj.group(2)
			if (month.isdigit()) :
				month = MONTH_ARRAY[int(month)]
			yearTime = matchObj.group(3)
			if (":" in yearTime) :
				year = TIME_DEFAULT_YEAR
				sp = yearTime.split(":")
				hour = sp[0]
				minutes = sp[1]
			else :
				year = yearTime
				hour = "0"
				minutes = "0"
			self.set_time(day, month, year, hour, minutes, None)
		else :
			assert False, "Can't pull time from %s" % line

	def get_debug_string(self) :
		return File.get_base_debug_line(self)

	def is_link(self, line) :
		return line[0] == "l"

	def set_attributes_from_match_obj(self, matchObj) :
		self.permis = matchObj.group(1)
		self.size = int(matchObj.group(2))
		if ("->" in matchObj.group(3)) :
			self.name = matchObj.group(5)
			try :
				self.name.decode("utf-8")
				self.nameIsBin = 0
			except :
				self.name = sanitize_non_utf8(self.name)
				self.nameIsBin = 1

			self.realname = matchObj.group(6)
			try :
				self.realname.decode("utf-8")
				self.realnameIsBin = 0
			except :
				self.realname = sanitize_non_utf8(self.realname)
				self.realnameIsBin = 1

			assert self.realname != None, str(matchObj.groups())
			self.isLink = True
		else :
			self.name = matchObj.group(3)
			try :
				self.name.decode("utf-8")
				self.nameIsBin = 0
			except :
				self.name = sanitize_non_utf8(self.name)
				self.nameIsBin = 1
			self.realnameIsBin = 0
			self.realname = None
			self.isLink = False
		assert self.name != None

	def parse_line(self, line) :
		if (re.match(FULL_LINUX_1, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_1, line))
		elif (re.match(FULL_LINUX_2, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_2, line))
		elif (re.match(FULL_LINUX_3, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_3, line))
		elif (re.match(FULL_LINUX_4, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_4, line))
		elif (re.match(FULL_LINUX_5, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_5, line))
		elif (re.match(FULL_LINUX_6, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_6, line))
		elif (re.match(FULL_LINUX_7, line)) :
			self.set_attributes_from_match_obj(re.match(FULL_LINUX_7, line))
		else :
			self.isParsable = False
			return
		self.isParsable = True

class WindowsFile(File) :
	def __init__(self, line) :
		File.__init__(self)
		self.permis = "???"
		self.parse_line(line)
		self.parse_time(line)

	def parse_time(self, line) :
		matchObj = re.match(TIME_WINDOWS_1, line)
		assert matchObj != None
		month = matchObj.group(1)
		day = matchObj.group(2)
		year = matchObj.group(3)
		if (len(year) != 4) :
			if (year[0] == "9") :
				year = "19" + year
			else :
				year = "20" + year
		hour = matchObj.group(4)
		minutes = matchObj.group(5)
		amPm = matchObj.group(6)
		self.set_time(day, month, year, hour, minutes, amPm)

	def get_debug_string(self) :
		return File.get_base_debug_line(self)

	def parse_line(self, line) :
		self.isLink = False

		if (re.match(FULL_WINDOWS_1, line)) :
			matchObj = re.match(FULL_WINDOWS_1, line)
			self.size = matchObj.group(2)
			self.name = matchObj.group(3)
			try :
				self.name.decode("utf-8")
				self.nameIsBin = 0
			except :
				self.name = sanitize_non_utf8(self.name)
				self.nameIsBin = 1

			self.realnameIsBin = 0
			self.realname = None
			self.isParsable = True
		else :
			self.isParsable = False

class VxWorksFile(File) :
	def __init__(self, line) :
		File.__init__(self)
		self.permis = "???"
		self.parse_line(line)
		self.parse_time(line)

	def parse_time(self, line) :
		matchObj = re.match(TIME_VXWORKS_1, line)
		assert matchObj != None
		month = matchObj.group(1)
		day = matchObj.group(2)
		year = matchObj.group(3)
		hour = matchObj.group(4)
		minutes = matchObj.group(5)
		self.set_time(day, month, year, hour, minutes, None)

	def get_debug_string(self) :
		return File.get_base_debug_line(self)

	def parse_line(self, line) :
		self.isLink = False

		if (re.match(FULL_VXWORKS_1, line)) :
			matchObj = re.match(FULL_VXWORKS_1, line)
			self.size = matchObj.group(2)
			self.name = matchObj.group(3)
			try :
				self.name.decode("utf-8")
				self.nameIsBin = 0
			except :
				self.name = sanitize_non_utf8(self.name)
				self.nameIsBin = 1
			self.realnameIsBin = 0
			self.realname = None
			self.isParsable = True
		else :
			self.isParsable = False
