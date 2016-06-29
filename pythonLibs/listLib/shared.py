UNSET = "<<<<< UNSET >>>>>"

ISO_TIME_FMT = "%Y-%m-%dT%H:%M:%SZ"
TIME_DEFAULT_YEAR = "9999"
MONTH_ARRAY = [None, "Jan", "Feb", "Mar", "Apr", "May", "June", "July", "Aug", "Sep", "Oct", "Nov", "Dec"]

def remove_empties(aList) :
	newList = []
	for anEle in aList :
		if (anEle != "") :
			newList.append(anEle)
	return newList

def sanitize_non_utf8(string) :
	temp = str([string])
	assert temp[0] == "[", temp
	if (temp[1] == "u") :
		assert temp[2] == "'" or temp[2] == '"', temp
	else :
		assert temp[1] == "'" or temp[1] == '"', temp
	assert temp[-1] == "]", temp
	assert temp[-2] == "'" or temp[-2] == '"', temp

	assert temp[2:-2] != None
	return temp[2:-2]

def sanitize_path(path) :
	assert path[0] == "/"
	assert path[-1] == "/"

	spPath = path.split("/")
	for i in range(len(spPath)) :
		part = spPath[i]
		try :
			part.decode("utf-8")
		except :
			spPath[i] = sanitize_non_utf8(part)
	return "/".join(spPath)

####################################
# Regex's
####################################
MONTH_RE = "(?:(?:[Jj]an)|(?:[Ff]eb)|(?:[Mm]ar)|(?:[Aa]pr)|(?:[Mm]ay)|(?:[Jj]un)|(?:[Jj]ul)|(?:[Aa]ug)|(?:[Ss]ep)|(?:[Oo]ct)|(?:[Nn]ov)|(?:[Dd]ec)|(?:[1-9])|(?:1[012]))"
USER_GROUP_RE = r"[-+\da-zA-Z_\\\.]+(?:\$)?"
TIME_YEAR_RE = "(?:(?:\d{4})|(?:\d{1,2}:\d{1,2}(?::\d{2})?))"
NAME_RE = "(((?:(?!->).)+)|(?:(.+) +(?:->) +(.+)))"
LINUX_PERMISSIONS_RE = "^[-pldcsb][-rwx]{2}[-rwxsS][-rwx]{2}[-rwxsS]([-rwx]{2}[-tTrwx])"

# Standard Windows
FULL_WINDOWS_1 = "^" + \
	"\d{2}-\d{2}-\d{2,4} +" + \
	"\d{2}:\d{2}(?:(?::\d{2})|(?:[AP]M))?( +)" + \
	"((?:\d+ +)|(?:<DIR>(?: ){10}))" + \
	"(.*)" + \
	"$"

TIME_WINDOWS_1 = "^(\d{2})-(\d{2})-(\d{2,4}) +(\d{2}):(\d{2})([AP]M)?.*$"


# year and time are interchangable for all linux types

# Standard Linux
FULL_LINUX_1 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	USER_GROUP_RE + " +" + \
	USER_GROUP_RE + " +" + \
	"(-?\d+) +" + \
	MONTH_RE + " +"+ \
	"\d{1,2} +" + \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

TIME_LINUX_1_2_4_5_6_7 = "^.* +\d+ +(" + MONTH_RE + ") +(\d{1,2}) +(" + TIME_YEAR_RE + ") +.*$"
TIME_LINUX_3 = "^.*(\d{1,2})(?:\.)? +(" + MONTH_RE + ") +(" + TIME_YEAR_RE + ") +.*$"

# Linux w/ extra 2 columns
FULL_LINUX_2 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	USER_GROUP_RE + " +" + \
	"(?:(?:.)|(?:.x[0-9a-zA-Z]{2})) +" + \
	USER_GROUP_RE + " +" + \
	"(?:(?:.)|(?:.x[0-9a-zA-Z]{2})) +" + \
	"(\d+) +" + \
	MONTH_RE + " +"+ \
	"\d{1,2} +" + \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

# Linux w/ day before month
FULL_LINUX_3 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	USER_GROUP_RE + " +" + \
	USER_GROUP_RE + " +" + \
	"(\d+) +" + \
	"\d{1,2}\. +" + \
	MONTH_RE + " +"+ \
	"\d{1,2}:\d{1,2} +" + \
	NAME_RE + \
	"$"

# Linux w/ no group
FULL_LINUX_4 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	USER_GROUP_RE + " +" + \
	"(\d+) +" + \
	MONTH_RE + " +"+ \
	"\d{1,2} +" + \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

# Linux with no ref count
FULL_LINUX_5 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	USER_GROUP_RE + " +" + \
	USER_GROUP_RE + " +" + \
	"(\d+) +" + \
	MONTH_RE + " +"+ \
	"\d{1,2} +" + \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

# Linux w/ question marks for user/group
FULL_LINUX_6 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	"(?:(?:\(\?\))|(?:\*)|" + USER_GROUP_RE + ")" + " +" + \
	"(?:(?:\(\?\))|(?:\*)|" + USER_GROUP_RE + ")" + " +" + \
	"(\d+) +" + \
	MONTH_RE + " +"+ \
	"\d{1,2} +" + \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

# Linux w/ unk before size
FULL_LINUX_7 = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	"(?:" + USER_GROUP_RE + " +" + ")?" + \
	USER_GROUP_RE + " +" + \
	"\d+, +" + \
	"(\d+) +" + \
	MONTH_RE + " +"+ \
	"\d{1,2} +" + \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

FULL_LINUX_UNPARSED = "^" + \
	LINUX_PERMISSIONS_RE + " +" + \
	"\d+ +" + \
	USER_GROUP_RE + " +" + \
	USER_GROUP_RE + " +" + \
	"(\d+) +" + \
	"\d{1,2} +" + \
	"\d{2}" + " +"+ \
	TIME_YEAR_RE + " +" + \
	NAME_RE + \
	"$"

# Standard VX Works
FULL_VXWORKS_1	= "^" + \
	"( *)" + \
	"(\d+) +" + \
	MONTH_RE + "-\d{2}-\d{4} +" + \
	"\d{2}:\d{2}:\d{2} +" + \
	"(.+?) *" + \
	"(<DIR>)?" + \
	"$"

TIME_VXWORKS_1 = "^.*(" + MONTH_RE + ")-(\d{2})-(\d{4}) +(\d{2}):(\d{2}).*"
