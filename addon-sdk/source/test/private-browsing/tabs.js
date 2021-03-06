'use strict';

const { Ci } = require('chrome');
const { openTab, closeTab } = require('sdk/tabs/utils');
const browserWindows = require('sdk/windows');
const { getOwnerWindow } = require('sdk/private-browsing/window/utils');

exports.testIsPrivateOnTab = function(test) {
  let window = browserWindows.activeWindow;
  let chromeWindow = getOwnerWindow(window);
  test.assert(chromeWindow instanceof Ci.nsIDOMWindow, 'associated window is found');
  test.assert(!pb.isPrivate(chromeWindow), 'the top level window is not private');

  let rawTab = openTab(chromeWindow, 'data:text/html,<h1>Hi!</h1>', {
    isPrivate: true
  });

  // test that the tab is private
  test.assert(rawTab.browser.docShell.QueryInterface(Ci.nsILoadContext).usePrivateBrowsing);
  test.assert(pb.isPrivate(rawTab.browser.contentWindow));
  test.assert(pb.isPrivate(rawTab.browser));

  closeTab(rawTab);
}
