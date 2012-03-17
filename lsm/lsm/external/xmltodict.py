#This code taken from recipe
#http://code.activestate.com/recipes/573463-converting-xml-to-dictionary-and-back/
#Modified slightly to remove namespace and number of other small details
#Licensed: PSF

from xml.etree import ElementTree

def _ns(tag):
    return tag[tag.find('}')+1:]

class XmlDictObject(dict):
    """
    Adds object like functionality to the standard dictionary.
    """
    def __init__(self, initdict=None):
        if initdict is None:
            initdict = {}
        dict.__init__(self, initdict)

    def __getattr__(self, item):
        return self.__getitem__(item)

    def __setattr__(self, item, value):
        self.__setitem__(item, value)

    def __str__(self):
        if self.has_key('_text'):
            return self.__getitem__('_text')
        else:
            return ''

    @staticmethod
    def Wrap(x):
        """
        Static method to wrap a dictionary recursively as an XmlDictObject
        """
        if isinstance(x, dict):
            return XmlDictObject((k, XmlDictObject.Wrap(v)) for (k, v) in x.iteritems())
        elif isinstance(x, list):
            return [XmlDictObject.Wrap(v) for v in x]
        else:
            return x

    @staticmethod
    def _UnWrap(x):
        if isinstance(x, dict):
            return dict((k, XmlDictObject._UnWrap(v)) for (k, v) in x.iteritems())
        elif isinstance(x, list):
            return [XmlDictObject._UnWrap(v) for v in x]
        else:
            return x

    def UnWrap(self):
        """
        Recursively converts an XmlDictObject to a standard dictionary and returns the result.
        """
        return XmlDictObject._UnWrap(self)

def _ConvertDictToXmlRecurse(parent, dictitem):
    assert type(dictitem) is not type([])

    if isinstance(dictitem, dict):
        for (tag, child) in dictitem.iteritems():
            if str(tag) == '_text':
                parent.text = str(child)
            elif type(child) is type([]):
                # iterate through the array and convert
                for listchild in child:
                    elem = ElementTree.Element(tag)
                    parent.append(elem)
                    _ConvertDictToXmlRecurse(elem, listchild)
            else:
                elem = ElementTree.Element(tag)
                parent.append(elem)
                _ConvertDictToXmlRecurse(elem, child)
    else:
        parent.text = str(dictitem)

def ConvertDictToXml(xmldict):
    """
    Converts a dictionary to an XML ElementTree Element
    """
    roottag = xmldict.keys()[0]
    root = ElementTree.Element(roottag)
    _ConvertDictToXmlRecurse(root, xmldict[roottag])
    return root

def _ConvertXmlToDictRecurse(node, dictclass):
    nodedict = dictclass()

    if len(node.items()) > 0:
        # if we have attributes, set them
        if'attrib' in nodedict:
            nodedict['attrib'].update(dict(node.items()))
        else:
            nodedict['attrib'] = {}
            nodedict['attrib'].update(dict(node.items()))
            #We get a collision so attributes get their own hash!
            #nodedict.update(dict(node.items()))

    for child in node:
        # recursively add the element's children
        newitem = _ConvertXmlToDictRecurse(child, dictclass)
        if nodedict.has_key(_ns(child.tag)):
            # found duplicate tag, force a list
            if type(nodedict[_ns(child.tag)]) is type([]):
                # append to existing list
                nodedict[_ns(child.tag)].append(newitem)
            else:
                # convert to list
                nodedict[_ns(child.tag)] = [nodedict[_ns(child.tag)], newitem]
        else:
            # only one, directly set the dictionary
            nodedict[_ns(child.tag)] = newitem

    if node.text is None:
        text = None
    else:
        text = node.text.strip()

    if len(nodedict) > 0:
    # if we have a dictionary add the text as a dictionary value (if there is any)
        if text is not None and len(text) > 0:
            nodedict['_text'] = text
    else:
        # if we don't have child nodes or attributes, just set the text
        nodedict = text

    return nodedict

def ConvertXmlToDict(root, dictclass=XmlDictObject):
    """
    Converts an ElementTree Element to a dictionary
    """
    return dictclass({_ns(root.tag): _ConvertXmlToDictRecurse(root, dictclass)})