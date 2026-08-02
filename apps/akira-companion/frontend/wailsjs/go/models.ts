export namespace backend {
	
	export class NatInfo {
	    mapping: string;
	    filtering: string;
	    externalIp: string;
	    error?: string;
	
	    static createFrom(source: any = {}) {
	        return new NatInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.mapping = source["mapping"];
	        this.filtering = source["filtering"];
	        this.externalIp = source["externalIp"];
	        this.error = source["error"];
	    }
	}
	export class PushOutcome {
	    result: string;
	    message: string;
	
	    static createFrom(source: any = {}) {
	        return new PushOutcome(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.result = source["result"];
	        this.message = source["message"];
	    }
	}
	export class Status {
	    duid: string;
	    hasToken: boolean;
	    tokenValid: boolean;
	    onlineId: string;
	    accountId: string;
	
	    static createFrom(source: any = {}) {
	        return new Status(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.duid = source["duid"];
	        this.hasToken = source["hasToken"];
	        this.tokenValid = source["tokenValid"];
	        this.onlineId = source["onlineId"];
	        this.accountId = source["accountId"];
	    }
	}

}

export namespace pair {
	
	export class SwitchInfo {
	    name: string;
	    host: string;
	    port: number;
	
	    static createFrom(source: any = {}) {
	        return new SwitchInfo(source);
	    }
	
	    constructor(source: any = {}) {
	        if ('string' === typeof source) source = JSON.parse(source);
	        this.name = source["name"];
	        this.host = source["host"];
	        this.port = source["port"];
	    }
	}

}

