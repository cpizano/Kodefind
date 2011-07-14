{
  'target_defaults': {
    'variables': {
      'win_release_Optimization%': '2', # 2 = /Os
      'win_debug_Optimization%': '0',   # 0 = /Od
	  
      # See http://msdn.microsoft.com/en-us/library/8wtf2dfz(VS.71).aspx
      'win_debug_RuntimeChecks%': '3',    # 3 = all checks enabled, 0 = off
	  
      # See http://msdn.microsoft.com/en-us/library/47238hez(VS.71).aspx
      'win_debug_InlineFunctionExpansion%': '',    # empty = default, 0 = off,
      'win_release_InlineFunctionExpansion%': '2', # 1 = only __inline, 2 = max
	  
      # VS inserts quite a lot of extra checks to algorithms like
      # std::partial_sort in Debug build which make them O(N^2)
      # See http://msdn.microsoft.com/en-us/library/aa985982(v=VS.80).aspx
      'win_debug_disable_iterator_debugging%': '0',

      # See http://msdn.microsoft.com/en-us/library/aa652367.aspx
      'win_release_RuntimeLibrary%': '0', # 0 = /MT (nondebug static)
      'win_debug_RuntimeLibrary%': '1',   # 1 = /MTd (debug static)

      # VCLinkerTool LinkIncremental values below:
      #   0 == default
      #   1 == /INCREMENTAL:NO
      #   2 == /INCREMENTAL
      # Debug links incremental, Release does not.
      #	  
	  'msvs_debug_link_incremental%': '2',
	  'msvs_debug_link_nonincremental%': '1',
    },  
    'default_configuration': 'Debug',
    'configurations': {
      # Abstract base configurations to cover common attributes.
      #
      'Common_Base': {
        'abstract': 1,
        'msvs_configuration_attributes': {
         'OutputDirectory': '$(SolutionDir)$(Platform)\\$(Configuration)\\',
         'IntermediateDirectory': '$(OutDir)\\obj\\$(ProjectName)',
         'CharacterSet': '1',
        },
      },
      'x86_Base': {
        'abstract': 1,
        'msvs_settings': {
          'VCLinkerTool': {
            'TargetMachine': '1',
          },
        },
        'msvs_configuration_platform': 'Win32',
      },
      'x64_Base': {
        'abstract': 1,
        'msvs_configuration_platform': 'x64',
        'msvs_settings': {
          'VCLinkerTool': {
            'TargetMachine': '17', # x86 - 64
          },
        },
      },
      'Debug_Base': {
        'abstract': 1,
		'defines': [
          'DEBUG',
          '_DEBUG',
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '<(win_debug_Optimization)',
            'PreprocessorDefinitions': ['_DEBUG'],
            'BasicRuntimeChecks': '<(win_debug_RuntimeChecks)',
            'RuntimeLibrary': '<(win_debug_RuntimeLibrary)',
			'GenerateDebugInformation': 'true'
            'conditions': [
              # According to MSVS, InlineFunctionExpansion=0 means
              # "default inlining", not "/Ob0".
              # Thus, we have to handle InlineFunctionExpansion==0 separately.
              ['win_debug_InlineFunctionExpansion==0', {
                'AdditionalOptions': ['/Ob0'],
              }],
              ['win_debug_InlineFunctionExpansion!=""', {
                'InlineFunctionExpansion':
                  '<(win_debug_InlineFunctionExpansion)',
              }],
              ['win_debug_disable_iterator_debugging==1', {
                'PreprocessorDefinitions': ['_HAS_ITERATOR_DEBUGGING=0'],
              }],
            ],
          },
          'VCLinkerTool': {
            'LinkIncremental': '<(msvs_debug_link_incremental)',
          },
          'VCResourceCompilerTool': {
            'PreprocessorDefinitions': ['_DEBUG'],
          },
        },
      },
	  'Release_Base': {
        'abstract': 1,
        'defines': [
          'NDEBUG',
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'Optimization': '<(win_release_Optimization)',
            'RuntimeLibrary': '<(win_release_RuntimeLibrary)',
            'conditions': [
              # According to MSVS, InlineFunctionExpansion=0 means
              # "default inlining", not "/Ob0".
              # Thus, we have to handle InlineFunctionExpansion==0 separately.
              ['win_release_InlineFunctionExpansion==0', {
                'AdditionalOptions': ['/Ob0'],
              }],
              ['win_release_InlineFunctionExpansion!=""', {
                'InlineFunctionExpansion':
                  '<(win_release_InlineFunctionExpansion)',
              }],
            ],
          },
          'VCLinkerTool': {
            'LinkIncremental': '1',
          },
        },
      },
      #
      # Concrete configurations
      #
      'Debug': {
        'inherit_from': ['Common_Base', 'x64_Base', 'Debug_Base'],
      },
      'Release': {
        'inherit_from': ['Common_Base', 'x64_Base', 'Release_Base'],
      },
	},
  },  
  'targets': [
    {
      'target_name': 'engine',
      'type': 'static_library',
      'sources': [
	    'src/target_version_win.h',
        'src/engine_v1_win.cc',
        'src/engine_v1_win.h',
      ],
      'dependencies': [
      ],
    },
    {
      'target_name': 'kodefind',
      'type': 'executable',
      'sources': [
	    'src/target_version_win.h',
        'src/gui_win.cc',
        'src/gui_win.h',
      ],
      'dependencies': [
        'engine',
      ],
      'msvs_settings': {
        'VCLinkerTool': {
          'SubSystem': 2,
        },
        # 'VCManifestTool': {
        #  'AdditionalManifestFiles': '$(ProjectDir)\\sawbuck.exe.manifest',
        # },
      },
    },
  ],
}
