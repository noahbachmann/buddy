"""Every app UI string the screen model relies on, in one place (step 0 findings, English UI).

The sanitizer keeps exactly these and replaces everything else, so a fixture never carries a name, a prompt, a
message, or an app-specific label. Only neutral words here: no app name (the repo is public, #66).
"""

import re

# Bottom tabs: content-desc of a View inside the tab's wrapper; the selected tab's wrapper is `selected`, the others
# `clickable`. The matches tab appends a badge: "Matches, 1 new message".
TAB_DISCOVER = "Discover"
TAB_LIKES = "Likes You"
TAB_MATCHES = "Matches"
TAB_PROFILE = "Profile Hub"
TABS = (TAB_DISCOVER, TAB_LIKES, TAB_MATCHES, TAB_PROFILE)
TAB_BADGE = re.compile(r"(\d+) new messages?")

# Feed (a profile on the Discover tab)
LIKE_PHOTO = "Like photo"
LIKE_PROMPT = "Like prompt"
LIKE_PHOTO_CAPTION = "Like photo prompt"   # a photo with a caption: still a photo
LIKE_BUTTONS = (LIKE_PHOTO, LIKE_PROMPT, LIKE_PHOTO_CAPTION)
SKIP = re.compile(r"Skip (.+)", re.S)            # floating button, carries the name
PHOTO = re.compile(r"(.+)'s photo", re.S)
VIDEO = re.compile(r"(.+)'s video", re.S)    # autoplays: the screen never idles while it's on it
LIKE_VIDEO = "Like video"                      # not in LIKE_BUTTONS: the bot can't watch it, never liked
VIDEO_PROMPT = re.compile(r"Video prompt: (.+)", re.S)   # a video answering a prompt: a video section with its question
VIDEO_CONTROLS = ("Show caption", "Unmute video", "Mute video")
VOICE_PROMPT = re.compile(r"Voice Prompt: (.+)", re.S)   # audio answering a prompt: a voice section, never liked
VOICE_CONTROLS = ("Play Voice Prompt", "Like voice prompt")
FRIEND_TAKE = re.compile(r"Friend's Take\. (.+)", re.S)   # a card her friends wrote, in a sideways carousel: never liked
LIKE_FRIEND_TAKE = re.compile(r"Like (.+)'s Friend's Take", re.S)
FRIENDS_HEADER = re.compile(r"From people close to (.+)", re.S)
ELAPSED = re.compile(r"Elapsed time: \d+ seconds?")      # a playing video's clock: changes every second
PROMPT = re.compile(r"Prompt: (.*?)\. Answer: (.*)", re.S)
POLL = re.compile(r"Prompt: (.+)", re.S)       # no answer (PROMPT checked first): a poll's question, options below
POLL_OPTION = re.compile(r"Option: (.+)", re.S)
PROMO_ACTION = "Try it"                       # the app's own feature ads inside a profile: not her details
UNDO_SKIP = "Undo last skip"
MORE = "More"
VERIFIED = "Selfie Verified"

# Comment sheet: inline panel under the liked section
EDIT_COMMENT = "Edit comment"                    # the field (its text is invisible to the tree: own keyboard reads it)
SEND_LIKE = "Send like"
SEND_LIKE_WITH = "Send like with message"

# Upsell sheet after "Send like": a paid upgrade instead of the like. Only the plain like is ever tapped
SEND_ANYWAY = "Send Like anyway"
OUT_OF_LIKES = "You're out of free likes for today"   # paywall after "send like": the day's likes are used up
CLOSE_SHEET = "Close sheet"                      # never tapped: unclear whether it drops the like

# Promo popups over the app (feature ads, upsells): a full-screen layer without tabs, its only safe exit this button
CLOSE = "Close"
# Paid-tier offer pinned over the matches list (2026-11-26): a clickable pill, its text and a ticking clock. Never tapped
OFFER = re.compile(r"\d+% off .+", re.S)
COUNTDOWN = re.compile(r"\d{1,2}(?::\d{2}){1,2}")   # the offer's clock: changes every second

# Lists
LIKES_HEADER = "Likes You"
NO_LIKES = "No likes yet"                        # prefix of the empty state's title
MATCHES_HEADER = "Matches"
YOUR_TURN = re.compile(r"Your turn \((\d+)\)")
THEIR_TURN = re.compile(r"Their turn \((\d+)\)")   # a collapsed section header (tap expands it): chats waiting on her

# Chat (classic views with resource ids)
CHAT_INPUT = "messageComposition"
CHAT_SEND = "sendChatButtonContainer"
CHAT_VOICE = "microphoneButton"                  # never tapped
CHAT_VOICE_DESC = "Record voice note"
CHAT_HEADER = "header"
BACK = "Back"
CHAT_TAB = "Chat"
CHAT_PROFILE_TAB = "Profile"
CHAT_HINT = "Send a message"
DOUBLE_TAP_HINT = "Double tap to like a message"  # the bot never double-taps in a chat
CHAT_NAME_VERIFIED = re.compile(r"(.+), Verified", re.S)   # the chat header's name with the verified badge
VERIFIED_BADGE = "Verified"
PHOTO_PROMPT = re.compile(r"Photo prompt: (.+)", re.S)    # a photo's caption (chat's Profile tab)
# The chat's More menu (a popup list): only Unmatch is ever tapped, by the unmatch move
MENU_HINT = "Press back to close"
MENU_WE_MET = "We Met"
MENU_UNMATCH = "Unmatch"
MENU_HIDE = "Hide"
MENU_REPORT = "Report"
MENU_ITEMS = (MENU_WE_MET, MENU_UNMATCH, MENU_HIDE, MENU_REPORT)
MESSAGE = re.compile(r" (.+?): (.*)\. ", re.S)   # " <name>: <text>. "
OUR_SENDER = "You"   # our own messages' sender in a chat (2026-11-25, live dump)
MATCH_ORIGIN = re.compile(r"You liked (.+)'s (photo|prompt)\.", re.S)
DATE_SEPARATOR = re.compile(
    r"(Today|Yesterday|Mon|Tue|Wed|Thu|Fri|Sat|Sun)\b.*\d{1,2}:\d{2}")

# Profile detail labels worth keeping in fixtures (icons with a content-desc next to a value). Others → placeholder,
# also the intentions label: it names the kind of app (public repo).
DETAIL_LABELS = frozenset({
    "Age", "Gender", "Sexuality", "Height", "Job", "Work", "School", "Hometown", "Location", "Politics", "Religion",
    "Relationship Type", "Pronouns", "Children", "Family Plans", "Drinking", "Smoking", "Pets", "Languages",
    "Languages Spoken",
    "Ethnicity", "Zodiac Sign",
})

# Strings kept verbatim by the sanitizer (besides the patterns above)
KEEP = frozenset({
    *TABS, *LIKE_BUTTONS, LIKE_VIDEO, *VIDEO_CONTROLS, *VOICE_CONTROLS, UNDO_SKIP, MORE, VERIFIED, EDIT_COMMENT, SEND_LIKE, SEND_LIKE_WITH,
    LIKES_HEADER, MATCHES_HEADER, CLOSE, SEND_ANYWAY, OUT_OF_LIKES, CLOSE_SHEET, BACK, CHAT_TAB, CHAT_PROFILE_TAB, CHAT_HINT, DOUBLE_TAP_HINT, PROMO_ACTION, *DETAIL_LABELS,
    CHAT_VOICE_DESC, VERIFIED_BADGE, MENU_HINT, *MENU_ITEMS,
})
