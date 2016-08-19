# from __future__ import print_function
import getopt
import sys
from collections import namedtuple
from rope.base.project import Project
from rope.contrib import codeassist
# from rope.contrib import autoimport
import os
import re
import pkg_resources
from distutils.version import LooseVersion as V

BUILDER_EXTENSION = '.ui'
ROPE_VERSION = ''

CompletionItem = namedtuple('CompletionItem', 'name info type scope location')


def new_completion_item(**i):
        return CompletionItem('_', '_', '_', '_', '_')._replace(**i)


class BuilderComplete(object):
    def __init__(
        self, project_path, resource_path,
        source_code, code_point, project_files
    ):
        self.code_point = code_point
        self.src_dir = os.path.join(
            project_path, os.path.dirname(resource_path)
        )
        self.source_code = source_code
        # grab all of the ui files from the project source files
        self.builder_files = \
            [
                f for f in project_files
                if f.endswith(BUILDER_EXTENSION) and os.path.exists(f)
            ]

        # suggest completions whenever someone types get_object(' in some form
        self.should_autocompelte_re = \
            re.compile(r'get_object\s*\(\s*[\'"](\w*)$')

    def get_proposals(self, skip_verify=False):
        """ return possible completions based on objects in ui files.
        skip_verify=False will check to make sure preceeding the
        current cursor position is something of the form 'get_object("' """
        starting_word = ""
        if skip_verify is False:
            curr_line = self.source_code[:self.code_point].split('\n')[-1]
            reg_search = self.should_autocompelte_re.search(curr_line)
            if not reg_search:
                return []
            starting_word = reg_search.groups()[0]

        possible_completions = self.get_all_builder_objects(self.builder_files)
        # return only the ones with valid start
        return [
            item for item in possible_completions
            if item.name.startswith(starting_word)
        ]

    def get_all_builder_objects(self, file_list):
        """ go through every file in file_list and extract all <object/>
        tags and return their ids """
        from xml.dom import minidom
        ret = []
        for f in file_list:
            md = minidom.parse(f)
            for e in md.getElementsByTagName('object'):
                item = new_completion_item(
                    name=e.getAttribute('id'), scope='external',
                    location=f, type='builder_object'
                )
                ret.append(item)
        return ret


class RopeComplete(object):
    def __init__(self, project_path, source_code, resource_path, code_point):
        self.project = Project(project_path)
        self.project.pycore._init_python_files()

        self.resource = self.project.get_resource(resource_path)
        self.source_code = source_code
        self.code_point = code_point

    def __del__(self):
        self.project.close()

    def get_proposals(self):
        ret = []
        proposals = codeassist.code_assist(
            self.project, self.source_code,
            self.code_point, resource=self.resource, maxfixes=10
        )
        proposals = codeassist.sorted_proposals(proposals)

        if V(ROPE_VERSION) <= V('0.9.2'):
            for proposal in proposals:
                ret.append(
                    new_completion_item(
                        name=proposal.name, scope=proposal.kind,
                        type=proposal.type
                    )
                )
        else:
            for proposal in proposals:
                ret.append(
                    new_completion_item(
                        name=proposal.name, scope=proposal.
                        scope, type=proposal.type
                    )
                )

        return ret

    def get_calltip(self):
        calltip = codeassist.get_doc(
            self.project, self.source_code, self.code_point,
            resource=self.resource, maxfixes=10
        )
        return calltip


def parse_arguments(args):
    """ Returns a dictionary containing all the parsed args
    and a string containing the source_code """
    ret = {}

    options, remainder = getopt.getopt(args, 'o:p:s:r:f:b:')
    for opt, arg in options:
        if opt in ('-o', '--option'):
            option_arg = arg
        elif opt in ('-p', '--project'):
            project_arg = arg
        elif opt == '-s':
            source_code_arg = arg
        elif opt == '-r':
            res_arg = arg
        elif opt == '-f':
            offset_arg = arg
        elif opt == '-b':
            builder_files_arg = arg

    ret['option'] = option_arg
    ret['project_path'] = str.replace(project_arg, 'file://', '') \
        if project_arg.startswith('file://') else project_arg
    ret['resource_path'] = os.path.relpath(res_arg, ret['project_path'])
    if ret['option'] != 'calltip':
        ret['project_files'] = builder_files_arg.split('|')
    ret['position'] = int(offset_arg)

    input = open(source_code_arg, 'r')
    ret['source_code'] = input.read()

    return ret

if __name__ == '__main__':
    try:
        ROPE_VERSION = pkg_resources.get_distribution('rope').version
    except pkg_resources.DistributionNotFound:
        try:
            ROPE_VERSION = pkg_resources.get_distribution('rope-py3k').version
        except pkg_resources.DistributionNotFound:
                print('|Missing python-rope module!|.|.|.|.|')
                sys.exit(1)
    try:
        args = parse_arguments(sys.argv[1:])

        suggestions = []
        calltip = ''
        if args['option'] == 'autocomplete':
            # get any completions Rope offers us
            comp = RopeComplete(
                args['project_path'], args['source_code'],
                args['resource_path'], args['position']
            )
            suggestions.extend(comp.get_proposals())
            """ see if we've typed get_object(' and if so, offer completions
            based upon the builder ui files in the project """
            comp = BuilderComplete(
                args['project_path'], args['resource_path'],
                args['source_code'], args['position'], args['project_files']
            )
            suggestions.extend(comp.get_proposals())
        elif args['option'] == 'calltip':
            calltip_obj = RopeComplete(
                args['project_path'], args['source_code'],
                args['resource_path'], args['position']
            )
            calltip = calltip_obj.get_calltip()

        for s in suggestions:
            print("|{0}|{1}|{2}|{3}|{4}|".format(
                s.name, s.scope, s.type, s.location, s.info
            ))
        print(calltip)
    except:
        pass
