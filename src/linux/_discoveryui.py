# Copyright (c) 2009 Bea Lam. All rights reserved.
#
# This file is part of LightBlue.
#
# LightBlue is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# LightBlue is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with LightBlue.  If not, see <http://www.gnu.org/licenses/>.

try:
    from Tkinter import *
except ImportError, e:
    raise ImportError("Error loading GUIs for selectdevice() and selectservice(), Tkinter not found: " + str(e))

# Provides services for controlling a listbox, tracking selections, etc.
class ListboxController(object):

    def __init__(self, listbox, cb_chosen):
        """
        Arguments:
            - cb_chosen: called when a listbox item is chosen -- i.e. when
              an item is double-clicked or the <Return> key is pressed while
              an item is selected.
        """
        self.setlistbox(listbox)
        self.cb_chosen = cb_chosen
        self.__alarmIDs = {}

    def setlistbox(self, listbox):
        self.listbox = listbox
        self.listbox.bind("<Double-Button-1>", lambda evt: self._chosen())
        self.listbox.bind("<Return>", lambda evt: lambda evt: self._chosen())

    # adds an item to the list
    def add(self, *items):
        for item in items:
            self.listbox.insert(END, item)

    # clears items in listbox & refreshes UI
    def clear(self):
        self.listbox.delete(0, END)

    # selects an item in the list.
    # pass index=None to deselect.
    def select(self, index):
        self._deselect()
        if index is not None:
            self.listbox.selection_set(index)
            self.listbox.focus()

    def _deselect(self):
        selected = self.selectedindex()
        if selected != -1:
            self.listbox.selection_clear(selected)

    def selectedindex(self):
        sel = self.listbox.curselection()
        if len(sel) > 0:
            return int(sel[0])
        return -1

    # starts polling the listbox for a user selection and calls cb_selected
    # when an item is selected.
    def track(self, cb_selected, interval=100):
        self._track(interval, -1, cb_selected)

    def _track(self, interval, lastindex, callback):
        index = self.selectedindex()
        if index != -1 and index != lastindex:
            callback(index)

        # recursively keep tracking
        self.__alarmIDs[id(self.listbox)] = self.listbox.after(
            interval, self._track, interval, index, callback)

    def stoptracking(self):
        for x in self.__alarmIDs.values():
            self.listbox.after_cancel(x)

    def focus(self):
        self.listbox.focus()

    def update(self):
        self.listbox.update()

    # called when a selection has been chosen (i.e. pressed return / dbl-click)
    def _chosen(self):
        index = self.selectedindex()
        if index != -1:
            self.cb_chosen(index)


# A frame which contains a listbox and has a title above the listbox.
class StandardListboxFrame(Frame):
    def __init__(self, parent, title, boxwidth=28):
        Frame.__init__(self, parent)
        self.pack()
        self.buildUI(parent, title, boxwidth)

    def buildUI(self, parent, title, boxwidth):
        bigframe = Frame(parent)
        bigframe.pack(side=LEFT, fill=BOTH, expand=1)

        self.titlelabel = Label(bigframe, text=title)
        self.titlelabel.pack(side=TOP)

        mainframe = Frame(bigframe, bd=1, relief=SUNKEN)
        mainframe.pack(side=BOTTOM, fill=BOTH, expand=1)

        scrollbar = Scrollbar(mainframe)
        scrollbar.pack(side=RIGHT, fill=Y)

        self.listbox = Listbox(mainframe, bd=1, exportselection=0)
        self.listbox.pack(fill=BOTH, expand=1)
        self.listbox.config(background="white", width=boxwidth)

        # attach listbox to scrollbar
        self.listbox.config(yscrollcommand=scrollbar.set)
        scrollbar.config(command=self.listbox.yview)

    def settitle(self, title):
        self.titlelabel.config(text=title)


class StatusBar(object):
    def __init__(self, parent, side=TOP, text=""):
        self.label = Label(parent, text=text, bd=0, pady=8)
        self.label.pack(side=side, fill=BOTH, expand=1)

    def settext(self, text):
        self.label.config(text=text)


# makes UI with top pane, status bar below top pane, and bottom pane.
# Probably should use a grid geometry manager instead, might be easier.
class LayoutFrame(Frame):
    def __init__(self, parent):
        Frame.__init__(self, parent, padx=10, pady=5)    # inner padding

        self.topframe = Frame(self)
        self.topframe.pack(side=TOP, fill=BOTH, expand=1)

        self.statusbar = StatusBar(self)

        self.lineframe = Frame(self, height=1, bg="#999999")
        self.lineframe.pack(side=TOP, fill=BOTH, expand=1)

        self.bottomframe = Frame(self, pady=5)
        self.bottomframe.pack(side=BOTTOM, fill=BOTH, expand=1)


# Abstract class for controlling and tracking selections for a listbox.
class ItemSelectionController(object):

    def __init__(self, listbox, cb_chosen):
        self.cb_chosen = cb_chosen
        self._controller = ListboxController(listbox, self._chosen)
        self._closed = False

    def getselection(self):
        index = self._controller.selectedindex()
        if index != -1:
            return self._getitem(index)
        return None

    # set callback=None to switch off tracking
    def trackselections(self, callback, interval=100):
        if callback is not None:
            self.cb_selected = callback
            self._controller.track(self._selected, interval)
        else:
            self._controller.stoptracking()

    def close(self):
        self._controller.stoptracking()
        self._closed = True

    def closed(self):
        return self._closed

    # called when an item is chosen (e.g. dbl-clicked, not just selected)
    def _chosen(self, index):
        if self.cb_chosen:
            self.cb_chosen(self._getitem(index))

    def _selected(self, index):
        if self.cb_selected:
            self.cb_selected(self._getitem(index))

        # move focus to this listbox
        self._controller.focus()

    def getitemcount(self):
        raise NotImplementedError

    def _getitem(self, index):
        raise NotImplementedError


class DeviceSelectionController(ItemSelectionController):

    # keep cache across instances (and across different sessions)
    _cache = []

    def __init__(self, listbox, cb_chosen):
        super(DeviceSelectionController, self).__init__(listbox, cb_chosen)
        self._discoverer = None
        self.__items = []
        self._loadcache()

    def close(self):
        self._stopdiscovery()
        DeviceSelectionController._cache = self.__items[:]
        super(DeviceSelectionController, self).close()

    def refreshdevices(self):
        self.__items = []
        self._controller.clear()
        self._controller.update()

        self._stopdiscovery()
        self._discoverer = _DeviceDiscoverer(self._founddevice, None)
        self._discoverer.find_devices(duration=10)

        #self._test("device", 0, 5)

    def _additem(self, deviceinfo):
        self.__items.append(deviceinfo)
        self._controller.add(deviceinfo[1]) # add name

    def getitemcount(self):
        return len(self.__items)

    def _getitem(self, index):
        return self.__items[index]

    def _founddevice(self, address, deviceclass, name):
        self._additem((address, name, deviceclass))

        # push updates to ensure names are progressively added to the display
        self._controller.listbox.update()

    def _loadcache(self):
        for item in DeviceSelectionController._cache:
            self._additem(item)

    def _stopdiscovery(self):
        if self._discoverer is not None:
            self._discoverer.cancel_inquiry()

    def _test(self, desc, n, max):
        import threading
        if n < max:
            dummy = ("00:00:00:00:00:"+str(n), "Device-" + str(n), 0)
            self._additem(dummy)
            threading.Timer(1.0, self._test, [desc, n+1, max]).start()


class ServiceSelectionController(ItemSelectionController):

    def __init__(self, listbox, cb_chosen):
        super(ServiceSelectionController, self).__init__(listbox, cb_chosen)
        self.__items = []

        # keep cache for each session (i.e. each time window is opened)
        self._sessioncache = {}

    def _additem(self, service):
        self.__items.append(service)
        self._controller.add(self._getservicedesc(service))

    def getitemcount(self):
        return len(self.__items)

    # show services for given device address
    # pass address=None to clear display
    def showservices(self, address):
        self.__items = []
        self._controller.clear()

        if address is None: return

        services = self._sessioncache.get(address)
        if not services:
            import lightblue
            services = lightblue.findservices(address)
            #services = [("", 1, "one"), ("", 2, "two"), ("", 3, "three")]
            self._sessioncache[address] = services

        if len(services) > 0:
            for service in services:
                self._additem(service)

    def _getitem(self, index):
        return self.__items[index]

    def _getservicedesc(self, service):
        address, port, name = service
        return "(%s) %s" % (str(port), name)


class DeviceSelector(Frame):

    title = "Select Bluetooth device"

    def __init__(self, parent=None):
        Frame.__init__(self, parent)
        self.pack()
        self._buildUI()
        self._selection = None
        self._closed = False

        self.master.bind("<Escape>", lambda evt: self._clickedcancel())

    def _buildUI(self):
        mainframe = LayoutFrame(self)
        mainframe.pack()
        self._statusbar = mainframe.statusbar

        self._buildlistdisplay(mainframe.topframe)
        self._buildbuttons(mainframe.bottomframe)

    def _buildlistdisplay(self, parent):
        self.devicesframe = StandardListboxFrame(parent, "Devices",
            boxwidth=38)
        self.devicesframe.pack(side=LEFT, fill=BOTH, expand=1)

        self._devicemanager = DeviceSelectionController(
            self.devicesframe.listbox, self._chosedevice)

    def _buildbuttons(self, parent):
        self._searchbutton = Button(parent, text="Search for devices",
            command=self._clickedsearch)
        self._searchbutton.pack(side=LEFT)

        self._selectbutton = Button(parent, text="Select",
            command=self._clickedselect)
        self._selectbutton.pack(side=RIGHT)
        self._selectbutton.config(state=DISABLED)

        self._cancelbutton = Button(parent, text="Cancel",
            command=self._clickedcancel)
        self._cancelbutton.pack(side=RIGHT)

    def run(self):
        try:
            self._trackselections(True)

            # run gui event loop
            self.mainloop()
        except Exception, e:
            print "Warning: error during device selection:", e

    def _trackselections(self, track):
        if track:
            self._devicemanager.trackselections(self._selecteddevice)
        else:
            self._devicemanager.trackselections(None)

    def getresult(self):
        return self._selection

    def _selecteddevice(self, device):
        self._selectbutton.config(state=NORMAL)

    def _chosedevice(self, device):
        self._clickedselect()

    def _clickedsearch(self):
        self._statusbar.settext("Searching for nearby devices...")
        self._searchbutton.config(state=DISABLED)
        self._selectbutton.config(state=DISABLED)
        self.update()

        self._devicemanager.refreshdevices()

        if not self._closed:
            self._statusbar.settext(
                "Found %d devices." % self._devicemanager.getitemcount())
            self._searchbutton.config(state=NORMAL)

    def _clickedcancel(self):
        self._quit()

    def _clickedselect(self):
        self._selection = self._devicemanager.getselection()
        self._quit()

    def _quit(self):
        self._closed = True
        self._devicemanager.close()
        #Frame.quit(self)   # doesn't close the window
        self.master.destroy()


class ServiceSelector(DeviceSelector):

    title = "Select Bluetooth service"

    def _buildlistdisplay(self, parent):
        self.devicesframe = StandardListboxFrame(parent, "Devices")
        self.devicesframe.pack(side=LEFT, fill=BOTH, expand=1)
        self._devicemanager = DeviceSelectionController(
            self.devicesframe.listbox, self._pickeddevice)

        # hack some space in between the 2 lists
        spacerframe = Frame(parent, width=10)
        spacerframe.pack(side=LEFT, fill=BOTH, expand=1)

        self.servicesframe = StandardListboxFrame(parent, "Services")
        self.servicesframe.pack(side=LEFT, fill=BOTH, expand=1)
        self._servicemanager = ServiceSelectionController(
            self.servicesframe.listbox, self._choseservice)

    def _trackselections(self, track):
        if track:
            self._devicemanager.trackselections(self._pickeddevice)
            self._servicemanager.trackselections(self._selectedservice)
        else:
            self._devicemanager.trackselections(None)
            self._servicemanager.trackselections(None)

    def _clearservices(self):
	self.servicesframe.settitle("Services")
        self._servicemanager.showservices(None)  # clear services list

    # called when a device is selected, or chosen
    def _pickeddevice(self, deviceinfo):
        self._clearservices()
        self._statusbar.settext("Finding services for %s..." % deviceinfo[1])
        self._selectbutton.config(state=DISABLED)
        self._searchbutton.config(state=DISABLED)
        self.update()

        self._servicemanager.showservices(deviceinfo[0])

        if not self._closed:    # user might have clicked 'cancel'
            self.servicesframe.settitle("%s's services" % deviceinfo[1])
            self._statusbar.settext("Found %d services for %s." % (
                                        self._servicemanager.getitemcount(),
                                        deviceinfo[1]))
            self._searchbutton.config(state=NORMAL)

    def _selectedservice(self, service):
        self._selectbutton.config(state=NORMAL)

    def _choseservice(self, service):
        self._clickedselect()

    def _clickedsearch(self):
        self._clearservices()
        self._trackselections(False)   # don't track selections while searching

        # do the search
        DeviceSelector._clickedsearch(self)

        # re-enable selection tracking
        if not self._closed:
            self._trackselections(True)

    def _clickedselect(self):
        self._selection = self._servicemanager.getselection()
        self._quit()

    def _quit(self):
        self._closed = True
        self._devicemanager.close()
        self._servicemanager.close()
        self.master.destroy()


# -----------------------------------

import select
import bluetooth

class _DeviceDiscoverer(bluetooth.DeviceDiscoverer):

    def __init__(self, cb_found, cb_complete):
        bluetooth.DeviceDiscoverer.__init__(self)  # old-style superclass
        self.cb_found = cb_found
        self.cb_complete = cb_complete

    def find_devices(self, lookup_names=True, duration=8, flush_cache=True):
        bluetooth.DeviceDiscoverer.find_devices(self, lookup_names, duration, flush_cache)

        # process until inquiry is complete
        self._done = False
        self._cancelled = False
        while not self._done and not self._cancelled:
            #print "Processed"
            readfiles = [self,]
            rfds = select.select(readfiles, [], [])[0]

            if self in rfds:
                self.process_event()

        # cancel_inquiry() doesn't like getting stopped in the middle of
        # process_event() maybe? so just use flag instead.
        if self._cancelled:
            bluetooth.DeviceDiscoverer.cancel_inquiry(self)

    def cancel_inquiry(self):
        self._cancelled = True

    def device_discovered(self, address, deviceclass, name):
        #print "device_discovered", address, deviceclass, name
        if self.cb_found:
            self.cb_found(address, deviceclass, name)

    def inquiry_complete(self):
        #print "inquiry_complete"
        self._done = True
        if self.cb_complete:
            self.cb_complete()

# -----------------------------------

# Centres a tkinter window
def centrewindow(win):
    win.update_idletasks()
    xmax = win.winfo_screenwidth()
    ymax = win.winfo_screenheight()
    x0 = (xmax - win.winfo_reqwidth()) / 2
    y0 = (ymax - win.winfo_reqheight()) / 2
    win.geometry("+%d+%d" % (x0, y0))

def setupwin(rootwin, title):
    # set window title
    rootwin.title(title)

    # place window at centre
    rootwin.after_idle(centrewindow, rootwin)
    rootwin.update()

# -----------------------------------

def selectdevice():
    rootwin = Tk()
    selector = DeviceSelector(rootwin)
    setupwin(rootwin, DeviceSelector.title)

    selector.run()
    return selector.getresult()

def selectservice():
    rootwin = Tk()
    selector = ServiceSelector(rootwin)
    setupwin(rootwin, ServiceSelector.title)

    selector.run()
    return selector.getresult()

if __name__ == "__main__":
    print selectservice()