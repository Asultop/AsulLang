/* --------------------------------------------------------------------------------------------
 * ALang Language Server
 * Provides syntax checking, diagnostics, and navigation features for ALang
 * ------------------------------------------------------------------------------------------ */

import {
	createConnection,
	TextDocuments,
	Diagnostic,
	DiagnosticSeverity,
	ProposedFeatures,
	InitializeParams,
	DidChangeConfigurationNotification,
	CompletionItem,
	CompletionItemKind,
	TextDocumentPositionParams,
	TextDocumentSyncKind,
	InitializeResult,
	Definition,
	Location,
	Range,
	Position
} from 'vscode-languageserver/node';

import {
	TextDocument
} from 'vscode-languageserver-textdocument';

// Create a connection for the server
const connection = createConnection(ProposedFeatures.all);

// Create a text document manager
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

let hasConfigurationCapability = false;
let hasWorkspaceFolderCapability = false;
let hasDiagnosticRelatedInformationCapability = false;

connection.onInitialize((params: InitializeParams) => {
	const capabilities = params.capabilities;

	hasConfigurationCapability = !!(
		capabilities.workspace && !!capabilities.workspace.configuration
	);
	hasWorkspaceFolderCapability = !!(
		capabilities.workspace && !!capabilities.workspace.workspaceFolders
	);
	hasDiagnosticRelatedInformationCapability = !!(
		capabilities.textDocument &&
		capabilities.textDocument.publishDiagnostics &&
		capabilities.textDocument.publishDiagnostics.relatedInformation
	);

	const result: InitializeResult = {
		capabilities: {
			textDocumentSync: TextDocumentSyncKind.Incremental,
			definitionProvider: true,
			completionProvider: {
				resolveProvider: true,
				triggerCharacters: ['.', '@']
			}
		}
	};
	
	if (hasWorkspaceFolderCapability) {
		result.capabilities.workspace = {
			workspaceFolders: {
				supported: true
			}
		};
	}
	
	return result;
});

connection.onInitialized(() => {
	if (hasConfigurationCapability) {
		connection.client.register(DidChangeConfigurationNotification.type, undefined);
	}
	if (hasWorkspaceFolderCapability) {
		connection.workspace.onDidChangeWorkspaceFolders(_event => {
			connection.console.log('Workspace folder change event received.');
		});
	}
});

interface ALangSettings {
	maxNumberOfProblems: number;
}

const defaultSettings: ALangSettings = { maxNumberOfProblems: 100 };
let globalSettings: ALangSettings = defaultSettings;

const documentSettings: Map<string, Thenable<ALangSettings>> = new Map();

connection.onDidChangeConfiguration(change => {
	if (hasConfigurationCapability) {
		documentSettings.clear();
	} else {
		globalSettings = <ALangSettings>(
			(change.settings.alangLanguageServer || defaultSettings)
		);
	}

	documents.all().forEach(validateTextDocument);
});

function getDocumentSettings(resource: string): Thenable<ALangSettings> {
	if (!hasConfigurationCapability) {
		return Promise.resolve(globalSettings);
	}
	let result = documentSettings.get(resource);
	if (!result) {
		result = connection.workspace.getConfiguration({
			scopeUri: resource,
			section: 'alangLanguageServer'
		});
		documentSettings.set(resource, result);
	}
	return result;
}

documents.onDidClose(e => {
	documentSettings.delete(e.document.uri);
});

documents.onDidChangeContent(change => {
	validateTextDocument(change.document);
});

// Simple token patterns for ALang
const KEYWORDS = new Set([
	'let', 'var', 'const', 'function', 'fn', 'return',
	'if', 'else', 'while', 'do', 'for', 'foreach', 'in',
	'break', 'continue', 'switch', 'case', 'default',
	'class', 'interface', 'extends', 'new', 'static',
	'async', 'await', 'go',
	'try', 'catch', 'finally', 'throw',
	'import', 'from', 'as', 'export',
	'match', 'yield', 'true', 'false', 'null'
]);

interface SymbolInfo {
	name: string;
	kind: 'function' | 'class' | 'variable' | 'interface';
	location: Location;
}

// Symbol table to track definitions
const symbolTable: Map<string, Map<string, SymbolInfo>> = new Map();

async function validateTextDocument(textDocument: TextDocument): Promise<void> {
	const settings = await getDocumentSettings(textDocument.uri);
	const text = textDocument.getText();
	const diagnostics: Diagnostic[] = [];
	const documentSymbols: Map<string, SymbolInfo> = new Map();

	// Parse and extract symbols
	const lines = text.split('\n');
	for (let i = 0; i < lines.length && diagnostics.length < settings.maxNumberOfProblems; i++) {
		const line = lines[i];
		
		// Check for function definitions
		const funcMatch = line.match(/\b(?:function|fn)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(/);
		if (funcMatch) {
			const funcName = funcMatch[1];
			const index = line.indexOf(funcName);
			documentSymbols.set(funcName, {
				name: funcName,
				kind: 'function',
				location: Location.create(
					textDocument.uri,
					Range.create(i, index, i, index + funcName.length)
				)
			});
		}

		// Check for class definitions
		const classMatch = line.match(/\bclass\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
		if (classMatch) {
			const className = classMatch[1];
			const index = line.indexOf(className);
			documentSymbols.set(className, {
				name: className,
				kind: 'class',
				location: Location.create(
					textDocument.uri,
					Range.create(i, index, i, index + className.length)
				)
			});
		}

		// Check for interface definitions
		const interfaceMatch = line.match(/\binterface\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
		if (interfaceMatch) {
			const interfaceName = interfaceMatch[1];
			const index = line.indexOf(interfaceName);
			documentSymbols.set(interfaceName, {
				name: interfaceName,
				kind: 'interface',
				location: Location.create(
					textDocument.uri,
					Range.create(i, index, i, index + interfaceName.length)
				)
			});
		}

		// Check for variable declarations
		const varMatch = line.match(/\b(let|var|const)\s+([a-zA-Z_][a-zA-Z0-9_]*)/);
		if (varMatch) {
			const varName = varMatch[2];
			const index = line.indexOf(varName);
			documentSymbols.set(varName, {
				name: varName,
				kind: 'variable',
				location: Location.create(
					textDocument.uri,
					Range.create(i, index, i, index + varName.length)
				)
			});
		}

		// Basic syntax checking
		// Check for unmatched brackets
		const openBrackets = (line.match(/\{/g) || []).length;
		const closeBrackets = (line.match(/\}/g) || []).length;
		const openParens = (line.match(/\(/g) || []).length;
		const closeParens = (line.match(/\)/g) || []).length;
		
		// Check for unclosed strings (simple check)
		const doubleQuotes = (line.match(/(?<!\\)"/g) || []).length;
		const singleQuotes = (line.match(/(?<!\\)'/g) || []).length;
		
		if (doubleQuotes % 2 !== 0 && !line.trim().startsWith('//') && !line.trim().startsWith('#')) {
			const diagnostic: Diagnostic = {
				severity: DiagnosticSeverity.Error,
				range: {
					start: { line: i, character: 0 },
					end: { line: i, character: line.length }
				},
				message: `Unclosed double quote`,
				source: 'alang'
			};
			diagnostics.push(diagnostic);
		}
		
		if (singleQuotes % 2 !== 0 && !line.trim().startsWith('//') && !line.trim().startsWith('#')) {
			const diagnostic: Diagnostic = {
				severity: DiagnosticSeverity.Error,
				range: {
					start: { line: i, character: 0 },
					end: { line: i, character: line.length }
				},
				message: `Unclosed single quote`,
				source: 'alang'
			};
			diagnostics.push(diagnostic);
		}
	}

	// Store symbols for this document
	symbolTable.set(textDocument.uri, documentSymbols);

	// Send the computed diagnostics to VSCode
	connection.sendDiagnostics({ uri: textDocument.uri, diagnostics });
}

connection.onDidChangeWatchedFiles(_change => {
	connection.console.log('We received a file change event');
});

// Go to Definition
connection.onDefinition((params: TextDocumentPositionParams): Definition | null => {
	const document = documents.get(params.textDocument.uri);
	if (!document) {
		return null;
	}

	const text = document.getText();
	const lines = text.split('\n');
	const line = lines[params.position.line];
	
	// Get the word at the cursor position
	const wordMatch = line.substring(0, params.position.character).match(/[a-zA-Z_][a-zA-Z0-9_]*$/);
	if (!wordMatch) {
		return null;
	}
	
	const word = wordMatch[0];
	
	// Look up in symbol table
	const documentSymbols = symbolTable.get(params.textDocument.uri);
	if (documentSymbols && documentSymbols.has(word)) {
		const symbol = documentSymbols.get(word)!;
		return symbol.location;
	}
	
	return null;
});

// Completion
connection.onCompletion(
	(_textDocumentPosition: TextDocumentPositionParams): CompletionItem[] => {
		const items: CompletionItem[] = [];
		
		// Add keywords
		KEYWORDS.forEach(keyword => {
			items.push({
				label: keyword,
				kind: CompletionItemKind.Keyword,
				data: keyword
			});
		});
		
		// Add symbols from current document
		const documentSymbols = symbolTable.get(_textDocumentPosition.textDocument.uri);
		if (documentSymbols) {
			documentSymbols.forEach((symbol, name) => {
				let kind: CompletionItemKind = CompletionItemKind.Variable;
				if (symbol.kind === 'function') kind = CompletionItemKind.Function;
				else if (symbol.kind === 'class') kind = CompletionItemKind.Class;
				else if (symbol.kind === 'interface') kind = CompletionItemKind.Interface;
				
				items.push({
					label: name,
					kind: kind,
					data: name
				});
			});
		}
		
		return items;
	}
);

connection.onCompletionResolve(
	(item: CompletionItem): CompletionItem => {
		if (KEYWORDS.has(item.data)) {
			item.detail = 'ALang keyword';
			item.documentation = `ALang keyword: ${item.data}`;
		}
		return item;
	}
);

// Make the text document manager listen on the connection
documents.listen(connection);

// Listen on the connection
connection.listen();
