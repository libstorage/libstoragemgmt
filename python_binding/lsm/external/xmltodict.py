# This code taken from recipe
# http://code.activestate.com/recipes/
# 573463-converting-xml-to-dictionary-and-back/
# Modified slightly to remove namespace and number of other small details
# Licensed: PSF

from xml.etree import ElementTree


def _ns(tag):
    return tag[tag.find('}') + 1:]


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
        if '_text' in self:
            return self.__getitem__('_text')
        else:
            return ''

    @staticmethod
    def wrap(x):
        """
        Static method to wrap a dictionary recursively as an XmlDictObject
        """
        if isinstance(x, dict):
            return XmlDictObject(
                (k, XmlDictObject.wrap(v)) for (k, v) in x.items())
        elif isinstance(x, list):
            return [XmlDictObject.wrap(v) for v in x]
        else:
            return x

    @staticmethod
    def _un_wrap(x):
        if isinstance(x, dict):
            return dict(
                (k, XmlDictObject._un_wrap(v)) for (k, v) in x.items())
        elif isinstance(x, list):
            return [XmlDictObject._un_wrap(v) for v in x]
        else:
            return x

    def un_wrap(self):
        """
        Recursively converts an XmlDictObject to a standard dictionary and
        returns the result.
        """
        return XmlDictObject._un_wrap(self)


def _convert_dict_to_xml_recurse(parent, dictitem):
    assert isinstance(dictitem, dict)

    if isinstance(dictitem, dict):
        for (tag, child) in dictitem.items():
            if str(tag) == '_text':
                parent.text = str(child)
            elif isinstance(child, list):
                # iterate through the array and convert
                for listchild in child:
                    elem = ElementTree.Element(tag)
                    parent.append(elem)
                    _convert_dict_to_xml_recurse(elem, listchild)
            else:
                elem = ElementTree.Element(tag)
                parent.append(elem)
                _convert_dict_to_xml_recurse(elem, child)
    else:
        parent.text = str(dictitem)


def convert_dict_to_xml(xmldict):
    """
    Converts a dictionary to an XML ElementTree Element
    """
    roottag = list(xmldict.keys())[0]
    root = ElementTree.Element(roottag)
    _convert_dict_to_xml_recurse(root, xmldict[roottag])
    return root


def _convert_xml_to_dict_recurse(node, dictclass):
    nodedict = dictclass()

    if len(list(node.items())) > 0:
        # if we have attributes, set them
        if'attrib' in nodedict:
            nodedict['attrib'].update(dict(list(node.items())))
        else:
            nodedict['attrib'] = {}
            nodedict['attrib'].update(dict(list(node.items())))
            # We get a collision so attributes get their own hash!
            # nodedict.update(dict(node.items()))

    for child in node:
        # recursively add the element's children
        newitem = _convert_xml_to_dict_recurse(child, dictclass)
        if _ns(child.tag) in nodedict:
            # found duplicate tag, force a list
            if isinstance(nodedict[_ns(child.tag)], list):
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
        # if we have a dictionary add the text as a dictionary value
        # (if there is any)
        if text is not None and len(text) > 0:
            nodedict['_text'] = text
    else:
        # if we don't have child nodes or attributes, just set the text
        nodedict = text

    return nodedict


def convert_xml_to_dict(root, dictclass=XmlDictObject):
    """
    Converts an ElementTree Element to a dictionary
    """
    return dictclass(
        {_ns(root.tag): _convert_xml_to_dict_recurse(root, dictclass)})
