import json
import os
import re
import sys
import string
import errno
import multiprocessing
import multiprocessing.pool
import subprocess
import shutil
import threading
import datetime
import time
import tempfile
import glob


# Do some things that Python doesn't: don't let stderr and stdout
# stomp on each other, and always write lines atomically including the newline.
threadsafe_print_lock = threading.Lock()
def threadsafe_print(f, s, force_newline=True):
	if not s.endswith('\n') and force_newline:
		s = s + '\n'
	with threadsafe_print_lock:
		f.write(s)
		f.flush()

def verbose_log(s):
	if globals().get('verbose', False):
		threadsafe_print(sys.stdout, str(s))

# Call another executable, with the option to capture and echo output.
# Returns a tuple: (returncode, stdout, stderr)
def system(cmd, shell=False, echo=True):
	if globals().get('verbose', False): 
		threadsafe_print(sys.stdout, str(cmd))
		echo=True
	
	process = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=shell)
	stdout = ""
	stderr = ""
	while process.poll() is None:
		out, err = process.communicate()
		if out:
			stdout += out
			if echo: threadsafe_print(sys.stdout, out, False)
		if err:
			stderr += err
			if echo: threadsafe_print(sys.stderr, err, False)
	return process.returncode, stdout, stderr

def copy(src, dest):
	shutil.copyfile(src, dest)

def move(src, dest) :
	shutil.move(src, dest)
	
def silent_remove(filename):
	try:
		if isinstance(filename, (list, tuple)):
			for f in filename:
				silent_remove(f)
		else:
			verbose_log("silent_remove %s" % filename)
			os.remove(filename)
	except OSError as e:
		if e.errno != errno.ENOENT:
			raise
	
def extension(profile):
	if	 profile == "vs_5_0": return ".vxo"
	elif profile == "ps_5_0": return ".pxo"
	elif profile == "cs_5_0": return ".cxo"
	elif profile == "gs_5_0": return ".gxo"
	elif profile == "es_5_0": return ".vxo"	 
	else: raise ValueError("Invalid profile: %s" % profile)

def orbis_profile(profile):
	if	 profile == "vs_5_0": return "sce_vs_vs_orbis"
	elif profile == "ps_5_0": return "sce_ps_orbis"
	elif profile == "cs_5_0": return "sce_cs_orbis"
	elif profile == "gs_5_0": return "sce_gs_on_chip_orbis"
	elif profile == "es_5_0": return "sce_vs_es_on_chip_orbis"	
	else: raise ValueError("Invalid profile: %s" % profile)

def fix_path(path):
	return os.path.normpath(path).replace('\\', '/')

def file_path(filedir, filename):
	return fix_path(os.path.join(filedir, filename))
	
def ensure_path(path):
	if not os.path.isdir(path):
		os.makedirs(path)

def entry_paths(dest_dir, entry, suffix, profile):
	dst = file_path(dest_dir, entry) + suffix + extension(profile)
	sb = file_path(dest_dir, entry) + suffix + ".sb"
	txt = file_path(dest_dir, entry) + suffix + ".txt"
	return dst, sb, txt

def remove_comments(text):								# Remove comments
	def replacer(match):
		s = match.group(0)
		if s.startswith('/'):							# line or block comment
			return re.sub(r'[^\n]+', '', s)				#	return just the newlines to keep our line numbering correct
		else:											# a string, which our RE matches in case it contains an embedded comment
			return s
	pattern = re.compile(
		r"""
				  //    .*?                   $			# Double slash starts a line comment
			|    /\*    .*?                  \*/		# Slash-star starts a block comment
			|     '     (?: \\. | [^\\'])*    '			# Single-quote starts a string, which goes until the next unescaped single quote
			|     "     (?: \\. | [^\\"])*    "			# Double-quote starts a string, which goes until the next unescaped double-quote
		""",
		re.DOTALL | re.MULTILINE | re.VERBOSE)
	return re.sub(pattern, replacer, text)

def parse_includes(file):
	with open(file, 'r') as f:
		text = f.read()
	lines = remove_comments(text).splitlines()
	
	include_info = []
	pattern = re.compile(r'''  \s* \# \s* include \s+ "([^"]+)"   ''', re.VERBOSE)
	
	lineno = 0
	for l in lines:
		lineno += 1
		m = pattern.search(l)
		if m:
			include_info.append({'raw-path':m.group(1), 'line':l, 'file':file, 'lineno':lineno})
	
	return include_info
	
def build(do_build, do_orbis, build_debug, build_trace, shdr_dir, copy_dir, dest_dir, cache_dir, defines):
	
	# shdr_dir	= ndlib/render/shaders
	# copy_dir	= $(BASETARGETDIR)/shaders/src
	# dest_dir	= $(BASETARGETDIR)/shaders/bytecode
	# cache_dir = $(BASETARGETDIR)/shaders/cache
	
	ensure_path(copy_dir)
	ensure_path(dest_dir)
	ensure_path(cache_dir)

	if do_orbis:
		orbis_sdk_dir	= os.getenv('SCE_ORBIS_SDK_DIR')
		game_shader_dir = os.getenv('GAMESHADERDIR')
		game_name = os.getenv('GAMENAME')

		if orbis_sdk_dir is None:
			raise IOError, "Environment var SCE_ORBIS_SDK_DIR missing!"

		if game_shader_dir is None:
			raise IOError, "Environment var GAMESHADERDIR missing!"

		orbis_sdk_dir = fix_path(orbis_sdk_dir)
		game_shader_dir = fix_path(game_shader_dir)

		shdcomp = orbis_sdk_dir + "/host_tools/bin/orbis-wave-psslc.exe"
		shdasm = orbis_sdk_dir + "/host_tools/bin/orbis-cu-as.exe"
		sb_dump = orbis_sdk_dir + "/host_tools/bin/orbis-sb-dump.exe"
		define_flag = "-D"
		force_includes = [ file_path(shdr_dir, "hlsl2pssl.fxi") ]
		include_search_paths = [ game_shader_dir + "/include" ]

        # Tempoaraily disable sndbs
		use_dbs = False
		#dbsbuild = 'C:/Program Files (x86)/SCE/Common/SN-DBS/bin/dbsbuild.exe'
		#if os.path.isfile(dbsbuild):
		#	use_dbs = True
		#	print('Compiling shaders with SN-DBS')
		#else:
		#	use_dbs = False
	else:
		shdcomp = "$(SHARED_SRC)/external/directx/Utilities/bin/x64/fxc.exe"
		define_flag = "/D"
		force_includes = []
		include_search_paths = []

	files_json = file_path(shdr_dir, 'files.json')
	build_py   = file_path(shdr_dir, 'build.py')
	
	modtime_cache = {}		  # key = path, val = int(modtime)
	included_cache = {}		  # key = path, val = list(path)
	dep_cache = {}			  # key = path, val = list(path)
	compilation_failed = 0
	dbs_commands = []
	
	# Generate a compiler version file that shaders can depend on. We only write it
	# (and thus change the mod date) if the compilers are different.
	old_compiler_version = ''
	compiler_version_file = os.path.join(copy_dir, 'compiler-versions.txt')
	if os.path.isfile(compiler_version_file):
		with open(compiler_version_file, 'rb') as f:
			old_compiler_version = f.read()
	
	new_compiler_version = system([shdcomp, '--version'], echo=False)[1]
	if old_compiler_version != new_compiler_version:
		ensure_path(os.path.dirname(compiler_version_file))
		with open(compiler_version_file, 'wb') as f:
			f.write(new_compiler_version)

	# Get a unique ID for this machine
	unique_id = '%08X' % (hash(os.environ.get('USERNAME')) & ((1 << 32) - 1))
	
	def modtime(path):
		try:
			if path in modtime_cache:
				return modtime_cache[path]
			result = os.stat(path).st_mtime
			modtime_cache[path] = result
			return result
		except:
			return 0
	
	def resolve_include(ii):
		for isp in [shdr_dir] + include_search_paths:
			resolved_path = file_path(isp, ii['raw-path'])
			if os.path.isfile(resolved_path):
				return resolved_path
		
		threadsafe_print(sys.stderr, "shaders/build.py: warning: Could not resolve include '%s'" % (ii['raw-path']))
		threadsafe_print(sys.stderr, "shaders/build.py: warning:    from %s:%s  %s" % (ii['file'], ii['lineno'], ii['line']))
		raise IOError, "Could not resolve include '%s'" % (ii['raw-path'])
	
	# Returns a tuple: (list of paths, number of unresolved paths)
	def included(path):
		try:
			if path in included_cache:
				return included_cache[path]
			
			# Parse the includes from this file
			#  returns a list of dicts: {raw-path, line, file, lineno}
			raw_includes = parse_includes(path)
			
			# Resolve each one. This will raise if we can't find something.
			unresolved = 0
			for ii in raw_includes:
				try:
					ii['resolved-path'] = resolve_include(ii)
				except:
					unresolved += 1
			
			# Recurse through sub-includes.
			all_includes = [ii['resolved-path'] for ii in raw_includes if 'resolved-path' in ii]
			for di in all_includes:
				sub_includes, sub_unresolved = included(di)
				all_includes += sub_includes
				unresolved += sub_unresolved
			
			# Remember result and return
			included_cache[path] = all_includes, unresolved
			return all_includes, unresolved
		except:
			threadsafe_print(sys.stderr, "    in file included from " + path)
			raise
	
	with open(files_json, 'r') as f:
		data = json.load(f)

	starttime = datetime.datetime.now()

	queued_jobs = []
	for f in data:
		fx_file	 = f["File"]
		entries	 = f["Entries"]
		is_asm   = os.path.splitext(fx_file)[1].lower() == '.scu'

		src = file_path(shdr_dir, fx_file)
			
		# Get the includes out of this file
		includes, force_rebuild = included(src)
			
		# Dependencies are the source file, files.json, compilers, compiler version file, this python script, and all includes
		dependencies = [src, files_json, shdcomp, compiler_version_file, build_py] + includes
			
		if do_build:
			for e in entries:
				if len(e) >= 2:
					job = {}
					job['src'] = fix_path(src)
					job['entry']	 = e[0]
					job['profile']	 = e[1]
					job['suffix']	 = '' if len(e) < 3 else e[2]
					job['defs']		 = '' if len(e) < 4 else ''.join('%s%s=%s ' % (define_flag, p[0], p[1]) for p in zip(*[iter(e[3])]*2) if p[0] != '')	# Forgive me, that is some crazy nasty Python! :D
					job['orbis_opt'] = '' if len(e) < 5 else e[4]
						
					# Add the defines passed in on the command line
					job['defs'] += ''.join('%s%s ' % (define_flag, d) for d in defines)

					job['dst'], job['sb'], job['txt'] = entry_paths(dest_dir, job['entry'], job['suffix'], job['profile'])

					job['cache_dir'] = cache_dir + '/' + job['entry'] + job['suffix']
						
					job['log'] = "* Compiling shader %-60s from %s" % (job['entry'] + job['suffix'], job['src'])
					job['is_asm'] = is_asm
					job['compiler'] = shdcomp if not is_asm else shdasm
						
					# File existence
					dst_exists = os.path.isfile(job['dst'])
					if not dst_exists:
						force_rebuild = True
						
					# Old and new timestamp dictionaries
					new_deps = dict([(f, str(modtime(f))) for f in dependencies])
					job['depfile']		= job['dst'] + '.deps'
					job['dependencies'] = new_deps
					old_deps = {}
					if os.path.isfile(job['depfile']):
						try:
							with open(job['depfile']) as file:
								old_deps = dict([(f,t) for f,t in [line.split() for line in file]])
						except:
							pass
						
					# Enqueue the job if any dependencies have changed at all
					#  NB: these files may be +m, so we look for any difference
					if force_rebuild or new_deps != old_deps:
						queued_jobs.append(job)
				
		else:
			for e in entries:
				if len(e) >= 2:
					entry	= e[0]
					profile = e[1]
					suffix	= '' if len(e) < 3 else e[2]
						
					dst, sb, txt = entry_paths(dest_dir, entry, suffix, profile)
						
					silent_remove((dst, txt))
		
	endtime = datetime.datetime.now()
	verbose_log("Took %s to set up jobs!" % (endtime - starttime))

	# Due to some windows weirdness, it's apparently possible to call a file IO function, have it
	# complete and return successfully, but not actually have the io operation fully complete,
	# especially when multiple threads are performing IO simultaneously. For example, deleting the
	# shader cache dir and attempting to re-create it will occasionally fail as the delete operation 
	# has not completed when the create op begins.
	# To work around this, we first kick off jobs that do nothing but delete the cache directory, 
	# all on a single thread, then we sleep for a moment before starting up the real build jobs...
	def delete_cachedir_job(job):
		cache_dir = job['cache_dir']

		if (os.path.exists(cache_dir)):
			shutil.rmtree(cache_dir)
			verbose_log("Deleted shader cache dir '%s'" % (cache_dir))

	def finalize_job(job, failed):
		if failed:
			threadsafe_print(sys.stderr, "shaders/build.py: error: compilation of %s from %s failed!\n" % (job['entry'] + job['suffix'], job['src']))
			try:
				os.remove(job['dst'])  # delete the output to ensure we rebuild again next time
			except:
				pass
		else:
			# If compilation succeeded, write out the dep file
			with open(job['depfile'], 'w') as depfile:
				deps = '\n'.join([' '.join(i) for i in job['dependencies'].items()])
				depfile.write(deps)
			
			# Copy the shader and give it a .sb extension, so Razor GPU's Trace can find it
			if build_trace:
				copy(job['dst'], job['sb'])

			# For asm, sdb files are created in the same location as the binary so move it to the cache dir
			if job['is_asm']:
				cache_dir = job['cache_dir']
				sdbfilename = file_path(dest_dir, job['entry']) + job['suffix'] + '_*.sdb'
				for file in glob.glob(sdbfilename):
					move(file, cache_dir)

	# This is the function called to actually build a job
	def build_job(job):

		# Print out the file name and the entry point, so we can watch the build progress
		threadsafe_print(sys.stdout, job['dst'])
			
		cache_dir = job['cache_dir']

		# This used to fail occasionally, so I'm going to attempt doing it in a
		# loop to ensure that it is successful
		cacheDirRetries = 5
		retries = cacheDirRetries
		while retries > 0:
			retries -= 1
			try:
				if not os.path.isdir(cache_dir):
					os.mkdir(cache_dir)	
			except Exception as e:
				if retries == 0:
					threadsafe_print(sys.stderr, "shaders/build.py: error: could not create cache dir '%s' after %d attempts : %s\n" % (cache_dir, cacheDirRetries, e))
			else:
				break

		is_asm = job['is_asm']

		# Compilation parameters
		if not is_asm :
			params = ["-entry " + job['entry'],
					job['defs'],
					"-profile " + orbis_profile(job['profile']),
					"-o " + job['dst']]

			for fi in force_includes :
				params += ["-include", fi]
			for isp in include_search_paths :
				params += ["-I" + isp]

			params += [
					"-Wperf",
					"-O3",
					"-enablevalidation",
					"-max-user-extdata-count 240",
					"-cache -cachedir " + cache_dir,
					"-ttrace 1" if build_trace else "",
					"-Werror",
					# suppress a few warnings
					# warning D7525: Using barriers on diverging paths can lead to undefined behaviour
					# warning D6889: inlined sampler state initialization has been skipped
					# warning D6890: FX syntax annotation has been skipped
					# warning D6921: implicit conversion turns floating-point number into integer: 'float' to 'unsigned int'
					# warning D6922: implicit conversion turns integer into floating-point number: 'int' to 'float'
					# warning D6923: implicit vector type narrowing: 'float4' to 'float3'
					# warning D20087: unreferenced formal parameter
					# warning D20088: unreferenced local variable
					# error D7524: Derivatives are available only in pixel shaders, LOD0 will be always used
					# error D7520: Not able to achieve target occupancy
					"-Wsuppress=7525,6889,6890,6921,6922,6923,20087,20088,7524,7520",
					"-cache-user-source-hash " + unique_id,
					"-sbiversion 3",
					"-pssl2", # enable new front end
					job['orbis_opt'],
					job['src'] ]
		else:
			params = ["-e " + job['entry'],
					"-o " + job['dst'],
					"-q",
					"--cache",
					"-Werror",
					# suppress a few warnings
					# warning RUNUSE__ : Register vcc_hi value was unused
					# warning INOFX___ : All output register values from instruction WITH NO SIDE EFFECTS were overwritten before used on all code paths
					# warning EXTGCESP: Export to mrt_color1 components RGBA exports components which will be discarded based on ps_export_color_en(mrt_color1, "xy")
					"-Wdisable=RUNUSE__,INOFX___,EXTGCESP",
					job['src']]

		# Build the command line
		command_line = job['compiler'] + ' ' + ' '.join(params)
		#threadsafe_print(sys.stdout,command_line)
			
		# Run the compilation
		if use_dbs:
			dbs_commands.append(command_line)
			return 0
		else:
			result = system(command_line)[0]

			failed = result != 0

			finalize_job(job, failed)
			
			return 1 if failed else 0

	if queued_jobs:
		# User can set the SHADERJOBS environment variable to force single threaded execution
		num_cores = max(int(os.environ.get('SHADERJOBS', multiprocessing.cpu_count())), 1) if not use_dbs else 1

		# We always run the delete_cachedir_jobs single threaded, to hopefully help
		# avoid Windows not actually completing the IO ops before returning
		for job in queued_jobs:
			delete_cachedir_job(job)

		# Sleep for a second to make sure that all the previous IO ops are really
		# actually totally finished, for reals!
		time.sleep(1.0)

		# If we have more than one core, run multithreaded!
		if num_cores > 1:
			# Now do the work, spreading it across N threads, 1 for each core on the system
			num_threads = min(64, num_cores) # don't exceed 64 because Windows can't wait for more than 64 things at a time
						
			verbose_log("Executing %d queued jobs using %d threads..." % (len(queued_jobs), num_threads))

			# Multithreaded with multiprocessing.pool
			# This is really just a map(), but we use map_async + get(timeout) to work around a Python bug with ctrl-C:
			#  http://stackoverflow.com/questions/1408356/keyboard-interrupts-with-pythons-multiprocessing-pool
			pool = multiprocessing.pool.ThreadPool(num_threads)
			failed_jobs = sum(pool.map_async(build_job, queued_jobs, 1).get(999999))

		else:
			# Single-threaded
			verbose_log("Executing %d queued jobs single threaded..." % (len(queued_jobs)))

			failed_jobs = 0
			for job in queued_jobs:
				failed_jobs += build_job(job)

	if use_dbs and queued_jobs:
		dbs_project = 'shaders-' + game_name

		with tempfile.NamedTemporaryFile(mode='w', suffix='.json', prefix = 'shaders-report.' + game_name + '.', delete=False) as json_file:
			report_json = json_file.name
			
		with tempfile.NamedTemporaryFile(mode='w', suffix='.dbs', prefix = 'shaders.' + game_name + '.', delete=False) as dbs_file:
			shaders_dbs = dbs_file.name
			#print "shaders_dbs = %s" % (shaders_dbs, )
			dbs_script = '\n'.join(dbs_commands)
			dbs_file.write(dbs_script)

		dbs_command_line = '"' + dbsbuild + '" -p ' + dbs_project + ' -s ' + shaders_dbs + ' -report ' + report_json
		#threadsafe_print(dbs_command_line)

		result = system(dbs_command_line)[0]

		failed = result != 0
		if failed:
			threadsafe_print(sys.stderr, "shaders/build.py: error: batch compilation of shaders failed!\n")

		with open(report_json, 'r') as f:
			report = json.load(f)

		dst_to_job_map = {}
		for job in queued_jobs:
			dst_to_job_map[job['dst']] = job

		pattern = re.compile('-o[ \t]+(\"[^\"]+\"|[^ ]+)')

		completed_jobs = report["completed_jobs"]
		for f in completed_jobs:
			exit_code = f["exit_code"]
			command_line = f["command_line"]

			m = pattern.search(command_line)
			dst = m.group(1)

			job = dst_to_job_map[dst]
			failed = exit_code != 0
			failed_jobs += 1 if failed else 0
			finalize_job(job, failed)

		try:
			os.remove(shaders_dbs)
		except:
			pass

		try:
			os.remove(report_json)
		except:
			pass
			
	# Finally, copy files.json since it's loaded by the runtime
	if do_build:
		copy(files_json, file_path(copy_dir, 'files.json'))
		
	# Clean the compiler version on a clean
	if not do_build:
		silent_remove(compiler_version_file)

	if queued_jobs:
		# If something failed then stop the build with an error.
		if failed_jobs:
			builds = "build" if failed_jobs == 1 else "builds"
			threadsafe_print(sys.stderr, "shaders/build.py: error: %d %s failed" % (failed_jobs, builds))
			return 1

		# If we had any jobs, then we have built them successfully! Touch the "build-successful" file
		# to let the psarc build step know that it needs to rebuild the shader psarc!
		threadsafe_print(sys.stdout, "Done compiling ingame shaders!")

	return 0

# Verify args are valid!
args_valid = len(sys.argv) == 8
args_valid = args_valid and (sys.argv[1] == 'clean' or sys.argv[1] == 'build' or sys.argv[1] == 'debug' or sys.argv[1] == 'trace')
args_valid = args_valid and (sys.argv[2] == 'orbis' or sys.argv[2] == 'dx11')

if not args_valid:
	sys.exit("Usage: %s [build/clean/debug/trace] [orbis/dx11] srcdir copydir destdir \"defines\"" % sys.argv[0])
else:
	do_build  = sys.argv[1] != 'clean'
	do_debug  = sys.argv[1] == 'debug'
	do_trace  = sys.argv[1] == 'trace'
	do_orbis  = sys.argv[2] == 'orbis'
	
	shdr_dir  = fix_path(sys.argv[3])
	copy_dir  = fix_path(sys.argv[4])
	dest_dir  = fix_path(sys.argv[5])
	cache_dir = fix_path(sys.argv[6])

	defines	  = str(sys.argv[7]).split()

	# Verbose?
	verbose = os.getenv('VERBOSE') not in (None, '', '0')

	try:
		exit_code = build(do_build, do_orbis, do_debug, do_trace, shdr_dir, copy_dir, dest_dir, cache_dir, defines)
	except KeyboardInterrupt:
		exit_code = 130
	except:
		raise
	sys.exit(exit_code)
